# Radiant Source Editor — Virtualized Source Editing with Parser-Driven Highlighting

**Date:** 2026-10-06
**Status:** Implemented through P4 (record: [Radiant_Impl_Source_Editor](../impl/Radiant_Impl_Source_Editor.md)). P4 (2026-10-07) finished the outstanding items: pass 2 on the next frame (OQ16), reference links resolved across the window (CED16v4), blocks nested in containers, the §5.3 long-line guard, the §10 status line, row-wise caret motion and re-measuring under soft wrap, horizontal scrolling (§6.2), gutter clicks (§6.3), undo groups by pause and caret move (§5.2), and the §11 GTest ring; CED19 was revised to v2 to match the host as built. OQ17 was decided by the user the same day: soft wrap stays character wrap. OQ18 (files with NUL bytes) is open (§14.3). OQ6–OQ13 were decided by the user on 2026-10-06 (§14); CED14 and CED16 were revised to v2 the same day (user; old text in Appendix C). OQ14, raised during P0, was decided by the user the same day (§14.1). OQ15 and OQ16, raised during P1, were decided by the user on 2026-10-07 (§14.2): CED15v2 and CED16v3. CED18 follows OQ15 as CED18v2 (P2, same day). P3 (2026-10-07) built soft wrap (§6.4), find, indent and dedent, model clipboard, the Source↔Rich switch (§10) and the column maps of CED17; the impl record §7 lists what changed in this design.
**Scope:** a source-code editing surface for Lambda/Radiant that (1) edits large text files, a few MB and beyond, through a virtualized viewport that only materializes the visible lines; (2) highlights syntax for the formats Lambda already parses, by having the Lambda parser emit a trimmed node tree that spans the viewport; (3) renders plain text first and highlighted second, and keeps edited text highlighted by mapping the color spans through each edit until the exact highlighting is swapped in. Phase-1 POC highlights Markdown and HTML only. Out of scope: LSP/diagnostics, multi-cursor, folding, minimap, extensions, rich-preview sync (kept as the later plan in [Radiant_Code_Editor.md](Radiant_Code_Editor.md) §7).
**Builds on:**

- [Radiant_Code_Editor.md](Radiant_Code_Editor.md) — the CodePad proposal (2026-08-24): ledger CED1–CED10, the virtualization argument, the preview/sync plan. This document continues its ledger (CED11+) and **supersedes CED1, CED5, CED9 and CED10** (§3, Appendix B).
- [Radiant_Design_Edit_Mode.md](Radiant_Design_Edit_Mode.md) and [Radiant_Impl_Edit_Mode.md](../impl/Radiant_Impl_Edit_Mode.md) — `lambda edit` and the `lambda.edit` package (Phases 1–3 implemented): the format registry, session, shell, toolbar and save pipeline the source surface plugs into.
- [Lambda_Design_DOM_API.md](../Lambda_Design_DOM_API.md), [Lambda_Design_DOM_State.md](../Lambda_Design_DOM_State.md), [Lambda_Design_DOM_Editable.md](../Lambda_Design_DOM_Editable.md) — the `import dom` surface, reactive view state, and the Lambda-side editing seam.
- [Radiant_Editor_Stage4B.md](../editing/Radiant_Editor_Stage4B.md) — the three-layer model: native substrate (caret, selection, IME, clipboard, event routing) under script-owned editing logic.
- [RAD_06 — Inline and Text Layout](../../doc/dev/radiant/RAD_06_Inline_and_Text_Layout.md), `doc/Lambda_Sys_Func.md` §parse (the `sourcepos` option).

**Formal anchors:** S12.1.1v2 and S12.1.3 (effectful loading/saving in procedures; rendering is pure, mutation only in handlers); S9.1.4 (state lives in view state); S9.2.2 (read views are snapshots); S5.1.4 (no reference identity — the buffer is a value, versions are values); D7.2.1–D7.2.5 (package loading, state, the editing-policy boundary); D7.5.2 (persistence through the shared IO boundary); D7.5.3 (native mechanism vs script policy). No formal ruling is revised by this proposal.

**Ledger:** extends the editor area's `CED#` series (`doc/Doc_Convention.md` §4 — no new series). Decisions **CED11–CED22**, with CED14v2 revised 2026-10-06, CED15v2, CED18v2 and CED16v3 revised 2026-10-07, and CED16v4 and CED19v2 revised in P4 (2026-10-07); open questions **OQ6–OQ17** resolved, **OQ18** open (OQ1–OQ5 belong to the CodePad doc).

---

## 0. TL;DR

The source editor is a **fourth surface of the existing `lambda.edit` application**, written in Lambda like the Markdown, HTML and SVG surfaces, not a JS component (CED11). Its load-bearing choices:

1. **The buffer is a persistent chunked line list** — an immutable Lambda value; an edit rebuilds one chunk and the chunk index, undo is the previous value (CED12).
2. **The viewport is virtual and the editor owns scrolling.** A fixed number of line slots is rendered; the wheel, the scrollbar thumb and the caret move a `top_line` in state. No spacer element, no native scroll container, no pixel arithmetic over the whole file (CED13).
3. **Plain first, highlight next, edits stay colored.** Lines with no highlight yet, on open or when scrolled into view, render as plain text at once; the next frame (`dom.request_frame`) parses the window and renders them highlighted. From then on an edit maps the color spans through the change, extending, shrinking or shifting them, so typing never loses its colors. The next frame recomputes the exact spans off the keystroke path and swaps them in with one state assignment. The parser is never on the keystroke path (CED14v2).
4. **Highlighting is parser-driven.** `parse(chunks, {type, sourcepos: 'spans', window: [first, last], prescan})` returns flat span records for the constructs that intersect the window (CED15v2, CED16v4). A small per-format *span walker* in Lambda turns the spans into per-line token runs; CSS classes do the coloring (CED15–CED18). No stream tokenizers, no regex, no Tree-sitter.
5. **The parser enhancement is the only substantive native work**: chunked-buffer input read in place, a windowed parse whose boundary state and link labels are cached at chunk boundaries, inline-level spans for Markdown, and a source-ordered *lexical* mode for HTML (CED16v4–CED18v2). Other formats stay plain until someone adds a walker.

**Lineage.** The design takes **CodeMirror 6's document model** (an immutable line structure with structural sharing, edits as change sets, history as inverted changes) and **Monaco's viewport model** (editor-owned scrolling with its own scrollbar, a uniform line height, a fixed pool of line rows reused as the view scrolls). It departs from both on highlighting: neither editor highlights with the language's production parser, and this one does. §2.5 compares the three.

Milestones: P0 plain virtualized editor inside `lambda edit` → P1 Markdown highlighting → P2 HTML highlighting → P3 editing parity and polish (§12).

---

## 1. Goals and non-goals

**Goals**

- G1 — Edit text files of a few MB (≈100 K lines) with keystroke cost independent of file size, inside the existing `lambda edit` window: gutter with line numbers, caret and selection, undo/redo, Cmd-S save through the existing session pipeline.
- G2 — Syntax highlighting for the formats Lambda parses, driven by the Lambda parser's own node tree so the highlighter can never disagree with the parser. POC: Markdown and HTML.
- G3 — Two-pass rendering so highlighting is an overlay on an always-correct plain-text surface, never a precondition for showing or editing text.
- G4 — Everything drivable headlessly (`lambda edit --headless --event-file`) from day one, with a multi-MB stress fixture as the first gate.

**Non-goals (this design)**

- Soft wrap in P0–P2 (horizontal scroll first; §6.4 explains why editor-owned scrolling makes wrap a local change later). Decided 2026-10-06 (OQ11): P3.
- Multi-cursor; an own-drawn selection layer (native single-range selection is used, clamped to the window, as CED4 already decided).
- Live rich preview and source↔rendered sync — the CodePad plan (§7 there) still stands; it will consume the same `sourcepos` spans this design produces.
- Highlighting inside fenced code bodies by the fence's language (delegation); the fence is one `code` span in the POC. Decided 2026-10-06 (OQ12).
- Any format beyond Markdown and HTML; any change to parsers other than the markup engine and the HTML5 tokenizer.

---

## 2. Grounding — what exists today (verified 2026-10-06)

`file:line` cites drift; symbol names are the anchor.

### 2.1 `lambda edit` is a Lambda application, already shipping three surfaces

- `lambda.exe edit <file>` (`lambda/main.cpp`, the `edit` branch) opens the `lambda.edit` package: `lmd/package/edit/edit.ls` holds the **format registry** (`formats = [md.descriptor, html.descriptor, svg.descriptor]`, chosen by suffix), `open_document` reads the file once (`input(path, 'text')`), imports it through the descriptor, proves the round-trip, and builds the page. `session.ls` owns `save` (`output(text, target, {format: 'text', atomic: true})`, conflict detection against `disk_text`) and `reload`; `shell.ls` owns the page, toolbar, dialogs, and the handlers `beforeinput`/`editaction`/`selectionchange`/`keydown`/`mousedown`/`closerequest`.
- A descriptor is a map of `name`, `suffixes`, `surface`, `import_text`, `export_text`, `check_roundtrip`, `schema`, `unsupported_input_types`, toolbar groups. A new surface is a new descriptor plus a `surface_of` branch — the Edit Mode design's "extensible template contract" (§4 there) working as intended.
- The editing seam on the Lambda side: the `dom` package's UA behavior turns `beforeinput` into an `editaction` event carrying `input_type`, the target selection (`source_selection`/`source_pos`) and the payload; the shell applies the edit to its model and calls `edit_restore_selection` so the reactive re-render does not lose the caret. `contenteditable="plaintext-only"` is classified natively (`EDIT_MODE_PLAINTEXT_ONLY`) with paste normalized to text.
- Reactive views: `view <name> state a: …, b: … { template }` with `on <event>(evt)` handlers that assign state; the engine re-renders and diffs (the `render_map` reconciliation of `Lambda_Design_DOM_State.md`). A fixed-length `for` over slots reconciles positionally — exactly what a fixed-size line window needs. `dom.request_frame(owner, event_name)` (`fn_radiant_request_frame`, used by `lmd/package/slide/live.ls`) delivers a named event on the next frame — the deferral primitive pass 2 needs. `dom.scroll_operation`, `wheel` events with `deltaX/deltaY`, `get_selection` and `dom_rect_list` geometry exist on the waist (`lambda/dom/dom_api.def`).
- The demos `test/demo/tetris` and `test/demo/superlambda` show the house idiom for a stateful, keyboard-driven, per-frame Lambda UI with a fixed grid of positional slots.

### 2.2 The parser already has half of the span machinery

- `parse(str, {type: 'markdown', sourcepos: true})` (`lambda/runtime/lambda-eval.cpp` ~5880, `Input::source_positions`) tags **each top-level markup block** with `sourcepos: "L:C-L:C"` (1-based lines, byte columns, cmark convention) via `record_source_position` in `lambda/input/markup/block/block_document.cpp`. `lmd/package/edit/markdown.ls` already consumes it to keep view-only blocks as written.
- `MarkupParser` is **line-based**: `splitLines` materializes `lines[]`/`line_count`, `parse_document` loops `parse_block_element` from `current_line`. A link-definition pre-scan walks all lines first, tracking fenced-code state (`markup_parser.cpp` ~215–345). Inline content reaches `parse_inline_spans(parser, text)` as **stripped text** (list markers, quote markers and indentation removed) — inline nodes carry no columns today, and containers lose the column map.
- HTML5: the tokenizer tracks `Html5SourcePosition {line, column, offset}`; `html5_parse_ex` with `track_source_lines` puts a line-only `__source_line` on elements (opening tag only), which `build_dom_tree_from_element` copies to `DomElement::source_line`. `html5_tokenize_next` yields tokens one at a time; the tree builder then applies the WHATWG construction algorithm, which **reorders and synthesizes** (implied tags, foster parenting, adoption agency) — a tree walk is not a source-order walk.
- `SourceTracker` (`lambda/input/source_tracker.hpp`) gives O(1) line/column from an offset with a lazily built line index.

### 2.3 Layout constraints

- The old "500-wrap cap" in `layout_text` is gone: the guard at `radiant/layout_text.cpp` ~3863 is now a **stall detector** (500 retries without consuming text), so a long `<pre>` lays out fully. Virtualization is still mandatory: a single text node is re-laid-out whole on every edit, `load_text_doc` (`radiant/cmd_layout.cpp`) still renders a file as one `<pre>`, and `layout_node_visit_budget` scales from `MAX_LAYOUT_NODES = 50000`. One element per line for 100 K lines is out of the question; one element per *visible* line is trivial.
- Incremental layout skips clean block children; each line slot is a block child with its own text node, so an edit relays out one slot.
- No monospace uniform-advance fast path; an ASCII advance table and real font metrics exist. Column→x mapping is done in script from measured advances (§6.3).

### 2.4 The JS CodePad plan

`Radiant_Code_Editor.md` proposed a TS/JS CodePad with script stream tokenizers (CED5), a `lambda.exe edit` surface of its own (CED9) and a Lambda twin later (CED10). Since then `lambda edit` landed as a Lambda application and the editor family's center of gravity moved to `lmd/package/edit`. The highlighting approach also changes by user direction: parser-driven, not tokenizer-driven. Appendix B maps every CED ruling to its fate.

### 2.5 Prior art — CodeMirror 6 and VS Code / Monaco

The design borrows its **document model from CodeMirror 6** and its **viewport model from Monaco**, the editor component inside VS Code. CodeMirror 6 keeps text as an immutable balanced tree of line runs, applies edits as change sets, and keeps history as inverted changes; CED12 is a two-level version of the same idea, sized for a few MB. Monaco owns its scroll position, draws its own scrollbar, assumes a uniform line height, and renders absolutely positioned line rows; CED13 follows that shape rather than CodeMirror's native scroll container with an estimated height map, because editor-owned scrolling keeps all state in Lambda and makes headless fixtures deterministic. Highlighting is where the design departs from both (CED15v2): they each maintain a second grammar per language, while this design reuses the document's production parser.

| Concern | This design | CodeMirror 6 | VS Code / Monaco |
|---|---|---|---|
| Text storage | Immutable chunked line list, 256 lines per chunk, one index level (CED12) | Immutable balanced tree of line runs | Mutable piece tree over original and added buffers |
| History | Inverse deltas over immutable values | Inverted change sets over immutable state | Undo stack of edit operations |
| Scrolling | Editor-owned, own scrollbar, uniform line height (CED13) | Native scroll container with a height map and estimated heights | Own scrollable element and scrollbar, built around a uniform line height |
| Rendering | Fixed pool of line rows in a contenteditable host | Visible lines plus margin, with spacers for the rest | Absolutely positioned line divs, own cursor and selection |
| Input | `beforeinput` through Radiant's own substrate (CED19v2) | contenteditable plus a DOM mutation observer to absorb browser quirks | Hidden textarea |
| Selection | Native single range, clamped to the window (CED4, CED19v2) | Own-drawn, multi-range | Own-drawn, multi-cursor |
| Highlighting source | The document's production parser, over a window (CED15v2) | Lezer grammars written separately from any runtime parser | TextMate regex grammars, plus semantic tokens from language servers |
| Parse strategy | Re-parse a window each change, from boundary state cached at chunk boundaries (CED16v4) | Incremental: unchanged subtrees are reused | Line-by-line state chaining with cached per-line states |
| Highlight during an edit | Spans extended and shifted through the edit, exact spans swapped in on the next frame (CED14v2) | Short synchronous incremental parse, otherwise the old tree mapped through the change | Old tokens shifted, edited lines retokenized in the background |
| Target scale | A few MB | Millions of lines | Hundreds of MB, with tokenization switched off past a size threshold |

The comparison reflects the editors' public designs as understood on 2026-10-06; it is context for the rulings, not a compatibility target. The two rows above were revised on 2026-10-06 to close the gaps the first version had against these editors: a plain flash on every edit, and per-frame pre-scan work that grew with file size (CED14v2, CED16v3; old rulings in Appendix C). The remaining structural difference is that the window parse restarts at a heuristic safe line instead of reusing earlier parse results; the sliding-window fidelity test guards it (§11).

---

## 3. Decision ledger

| ID | Decision | Rationale / alternative rejected |
|----|----------|----------------------------------|
| **CED11** | The source editor is a **`lambda.edit` surface written in Lambda** (`lmd/package/edit/source*.ls`), registered as a format descriptor like `markdown`/`html`/`svg`. Supersedes CED1's "JS component" and CED10's "JS first, Lambda twin later". | `lambda edit` is already a Lambda app with the registry, session, save, dialogs and the `editaction` seam built; a JS editor would duplicate all of it and run interpreter-only. Lambda-first also makes the parser the natural highlighting engine (one call, no bridge). Confirmed by the user 2026-10-06 (OQ6). |
| **CED12** | **Buffer = persistent chunked line list**: `{chunks: [[line…]…], count, version, eol, final_newline}` with chunks of `CHUNK` lines (256). An edit rebuilds the touched chunk(s) and the chunk index; the result is a new value. **Undo/redo = a list of previous buffer values** plus selection snapshots; grouping by word/time as the rich editor does. | Lambda values are immutable (S5.1.4); a flat 100 K-string array would be copied per keystroke (≈0.8 MB) and churn the GC, a 5 MB string far worse. Chunking makes an edit O(CHUNK + count/CHUNK) and gives structural sharing for free, so history costs nothing extra. A rope/piece table in native code is rejected for v1: more code, and it would reintroduce reference-identity state the design rules keep out of view state. |
| **CED13** | **Virtual viewport with editor-owned scrolling.** The view renders `slots = ceil(viewport_h / line_h) + overscan` line rows (gutter + text in one row). State holds `top_line`; wheel, PageUp/Down, the own-drawn scrollbar thumb and caret-follow change it. The host is `overflow: hidden`; the wheel handler returns `'prevent-default'`. Fixed `line-height`, one monospace family, no wrap in v1. | A native scroll container with a height spacer needs `scrollTop` read/write and measured geometry on every scroll, and ties scrolling to pixels. Owning scroll keeps all state in Lambda (S9.1.4), makes headless fixtures deterministic, and is what lets soft wrap arrive later as a local change (§6.4). Positional slot reconciliation needs no keys. Confirmed 2026-10-06 (OQ8). |
| **CED14v2** | **Plain first, then highlight; provisional spans while editing; exact spans swapped in.** (1) A row whose line has no highlight runs, on open or when scrolling brings it into view, renders as **plain text** at once, and the handler requests a frame. (2) The frame handler (pass 2) parses the window, walks it and assigns `hl`; those rows re-render highlighted. (3) Once a row is highlighted, an edit keeps it highlighted: the edit handler maps `hl.runs` through the edit delta, **extending, shrinking, splitting or shifting the color spans** (§6.5), marks `hl` provisional, and re-renders with the mapped spans. Only inserted text the mapping cannot classify renders plain. (4) The edit handler requests a frame; pass 2 recomputes the exact runs off the keystroke path and **swaps** them in with one version-tagged state assignment, so no frame shows a half-updated window. | Typing never drops the colors on screen and never waits for the parser; v1 dropped every visible row to plain on each edit (Appendix C). Plain first keeps open and long scroll jumps instant, and scrolling within an already highlighted window costs no parse. Provisional spans are right for ordinary typing inside a token and wrong for at most one frame when an edit changes token structure, such as a backtick that opens a code span; the swap corrects it. Mapped and exact runs share glyph advances (§7.5), so the swap never shifts text. This is how CodeMirror 6 and VS Code behave (§2.5). Revised 2026-10-06 (user). |
| **CED15v2** | **Highlighting = parser-driven, over flat spans.** The parser reports where each construct lies as flat span records — kind (the construct's tag name), block flag, and start and end position — and a format's *span walker* `walk(spans, lines, window) → [per-line [{s, e, c}]]` maps kinds to CSS classes (`tok-*`). No stream tokenizers (supersedes CED5), no regex, no Tree-sitter, no second grammar for any format. Formats without a walker render plain. | One source of truth: the highlighter cannot disagree with the parser about what a construct is. Walkers are small and gain every parser fix for free. Spans rather than a trimmed Mark tree because every `parse()` keeps its `Input` until the runtime resets (4,000 parses of a 3 KB document reached 1 GB): a per-frame tree would accumulate, flat records copied to the GC heap do not (OQ15). Revised 2026-10-07 (user). |
| **CED16v4** | **Windowed parse over the chunked buffer, with a cached boundary state and each chunk's link labels.** `parse(chunks, {type, sourcepos: 'spans', window: [first, last], prescan: {states, valid, labels}})` returns `[kinds, spans, states, restart, labels]`: span kinds as symbols, spans as flat ints (kind, block, line, col, end line, end col; 0-based lines, code-point columns), the state per chunk boundary as flat ints (five per boundary), the line the parse began, and for Markdown one array of link reference labels per chunk. The parse runs on a private pool released before returning (CED15v2). (a) **Input** is the buffer's chunk array (arrays of lines, no terminators) or a flat line array, read in place: no join, no string copy; a windowed parse only touches the lines from its restart line to `last + LOOKAHEAD`. (b) **Boundary state** is the format's small data value for constructs that cross lines (fence, HTML block, front matter; for HTML: comment, CDATA, raw-text body, open tag), plus for Markdown the parser's own link-definition pre-scan state (its fence and paragraph state and the lines a definition carried past the boundary). The parser returns the state at the start of every chunk it scanned and accepts that list back through `prescan`, so it scans only from the nearest valid cached boundary. (c) **Labels.** Scanning a chunk also collects the labels it defines, with the parser's own pre-scan resumed from the chunk's boundary state. The window parse resolves reference links against every chunk's labels, so a definition outside the window counts as it does in a full parse. For Markdown the boundaries therefore extend to the document's end; the cache makes that a one-time scan per region. (d) The editor keeps `scan = {states, valid, labels}` beside the buffer. An edit marks every boundary after the first chunk it rebuilt as **suspect**; a chunk split or merge is mirrored in the states and the labels so later entries stay aligned, and a boundary inside a rebuilt range is **unknown** (negative kind), so it can never match. When the parser recomputes a suspect boundary and finds it equal to the cached one, that entry, all later ones and their chunks' labels become valid again. (e) As before: the **safe restart line** is at or before `first − overscan`; nodes are built only for blocks and tokens intersecting the window; parsing stops after `last`. Highlight-grade fidelity is the contract: token classes inside the window equal those of a full parse; structural differences outside it are allowed. | v1 flattened the buffer and pre-scanned from line 1 on every pass 2, linear in file size and in the caret's position (Appendix C). With boundary states a typing frame scans the edited chunk, usually converges at the next boundary, and parses only the window; a jump into an unvisited region scans once and caches. This is VS Code's per-line state cache at chunk grain (§2.5). States are values, not references (S5.1.4), so the cache needs no identity. The flat line array stays accepted for callers without a chunked buffer (OQ9). Revised 2026-10-06 (user); v3 2026-10-07 (user): flat span result (OQ15); v4 2026-10-07 (P4): labels per chunk close the one fidelity gap P3 left, reference links defined outside the window (0.28% of corpus lines), and unknown boundaries close a false convergence after a chunk split. |
| **CED17** | **Markdown spans at inline level.** With `sourcepos: 'inline'`, container and leaf blocks at every depth carry `sourcepos`, and inline nodes (`em`, `strong`, `code`, `a`, `img`, `del`, math, autolinks, HTML inlines) carry `sourcepos` covering **delimiters and content**. Text has no attribute: a run not covered by a child span inherits its parent's class. Containers carry a **column map** so stripped content maps back to source columns. | Highlighting needs the delimiters colored with their construct, which the covering-span rule gives at zero cost. The column map is the one fidelity risk (§8.1); the MVP fallback is block-level color inside containers whose stripping is not yet mapped — accepted for P1 with the fallback rate measured (OQ10, 2026-10-06). |
| **CED18v2** | **HTML spans come from the tokenizer alone.** `parse(chunks, {type: 'html', sourcepos: 'spans', window, prescan})` runs the HTML5 **tokenizer only**, with the tree builder bypassed, and returns CED16v4's flat result with the span kinds `tag` (the whole token, `<` to `>`), `tag-name`, `attr-name`, `attr-value` (quotes included), `comment`, `doctype`, `entity` and `raw` (an RCDATA, RAWTEXT or PLAINTEXT body). Text outside these is not a span. A token's range is the input the tokenizer consumed to produce it; attribute ranges come from the tokenizer's own state transitions. The lexer performs the one tree-builder duty the tokenizer needs: switching to RCDATA, RAWTEXT or PLAINTEXT after the start tags that require it. | The WHATWG tree is not in source order and synthesizes nodes that have no source; a highlighter wants what the author typed, where they typed it. Reusing the real tokenizer classifies `<script>` bodies, bogus comments and malformed tags exactly as the parser would. v2 2026-10-07: flat spans instead of an `<html-lexical>` tree, following OQ15 (CED15v2). |
| **CED19v2** | **Editing seam reuses the `lambda.edit` pipeline.** The window host is `contenteditable="true"`, bound as a model edit surface (`dom.bind_model_edit_surface`), and declines every rich-only intent, so its behavior is plain text. The surface handles `editaction` (insertText, insertLineBreak/insertParagraph, delete*, insertFromPaste, insertCompositionText, historyUndo/Redo, copy/cut with a model `clipboard_text`) by mapping the event's source selection to `{line, col}` through the projected window document, editing the buffer, and restoring the DOM selection from the model. Model selection is truth; the DOM range is its projection clamped to the window (CED4 kept). `keydown` owns navigation, select-all, undo/redo, save. Mouse → caret stays native inside the host and comes back through `selectionchange`. IME: the composing slot is not re-rendered until `compositionend`. | Same contract the rich surface already runs under (prevent, edit own model, reconcile, write caret). No native editing behavior returns (Stage 4B). v2 2026-10-07 (P4): the host as built; `radiant_model_edit_surface_bind` accepts only rich hosts, and declining rich intents gives the plain-text behavior `plaintext-only` was meant to give (impl record §1.1). |
| **CED20** | **Product surface.** The `source` descriptor claims the plain-text suffixes `lambda edit` does not yet open (`.txt .ls .json .yaml .yml .toml .xml .css .js .ts .tex .csv .ini .conf .log .sh .py …`); for `.md`/`.html`, which the rich surfaces claim, `lambda edit --source <file>` opens the source surface instead. Supersedes CED9's separate editor app. | One command, one shell, one save path. A Source↔Rich toggle inside a session is a later feature (OQ7: P3 at the earliest). `load_text_doc` in `view` remains a read-only viewer; it can later host the same window component read-only. |
| **CED21** | **Columns are code points in the model**, bytes in `sourcepos` (parser), and whatever unit the DOM seam reports at the boundary; the two conversions live in one helper module and are tested by property tests. | Lambda strings are UTF-8 and `len()` counts code points; `sourcepos` is cmark's byte column; DOM boundaries may be UTF-16 units. Converting at the edges keeps the editor's own arithmetic uniform. |
| **CED22** | **Testing rings**: Lambda unit tests (`test/lambda/edit/source_*.ls` + `.txt`) for buffer laws, column conversion, walkers (golden token runs); GTest for the windowed parse (span→substring fidelity; window ≡ full-parse restricted) over the Markdown/HTML corpora; headless UI fixtures (`test/ui/edit/source_*.json`) including a generated multi-MB stress file under `./temp/`. | House pattern; the stress fixture is the first deliverable because interpreter-side cost per keystroke is the main risk (§13). |

---

## 4. Architecture

```
┌──────────── lambda edit window (lmd/package/edit, Lambda) ─────────────────────────────┐
│  toolbar (shell.ls)                                                                      │
│  ┌─ source surface (source.ls) ───────────────────────────────────────────────────────┐ │
│  │ state: buf (CED12) · sel · top_line · hl {version, first, last, runs} · ime · geom  │ │
│  │ view:  N slots  [gutter n][ text: plain | spans ]   + own scrollbar thumb           │ │
│  │ handlers: editaction · selectionchange · keydown · wheel · mousedown/move/up        │ │
│  │           · resize · source_frame (pass 2) · compositionstart/end                   │ │
│  └────────────────────────────────────────────────────────────────────────────────────┘ │
│  session.ls: open/save/reload  ·  source_buffer.ls  ·  source_highlight.ls (walkers)    │
└───────────────▲──────────────────────────────────────────────────────▲───────────────────┘
                │ editaction / selectionchange / wheel / frame          │ parse(lines, {window, sourcepos})
   native substrate (Stage 4B Layer A): contenteditable classification, caret/selection paint,
   IME, clipboard, event routing, dom.request_frame, dom geometry      │
                                                            lambda/input: markup engine (windowed,
                                                            inline sourcepos) · html5 tokenizer (lexical)
```

Data flow for one keystroke: `editaction` → map target to `{line, col}` → `buf' = edit(buf, …)`, `sel'`, `history'`, `hl' = map_runs(hl, delta)` (provisional), `scan'` with suspect entries → state assigned → **pass 1** re-render: edited rows show the extended spans, rows below shift with them, and only inserted text the mapping cannot classify is plain → `edit_restore_selection` → `dom.request_frame(host, "source_frame")`. Next frame: `source_frame` → `parse(buf'.chunks, {…, window, prescan: scan'})` → exact runs and refreshed `scan` → **pass 2**: one state assignment swaps them in. A scroll inside `[hl.first, hl.last]` renders highlighted immediately; a scroll beyond it renders the new rows plain and requests a frame.

Files (proposed): `lmd/package/edit/source.ls` (descriptor, surface view, handlers), `source_buffer.ls` (CED12 structure, edits, history, line/col helpers), `source_view.ls` (window math, slot rendering, scrollbar), `source_highlight.ls` (walker registry, Markdown and HTML walkers, class vocabulary, `map_runs`), `source_units.ls` (CED21 conversions).

---

## 5. Buffer and history (CED12)

### 5.1 Structure

```lambda no-run
// no-run: sketch of the shape, not the final API
{ chunks: [["line 0", "line 1", …], ["line 256", …], …],   // CHUNK = 256 lines each, last may be short
  count: 100213,                                          // total lines
  version: 418,                                           // monotonic; bumps on every edit
  eol: "\n",                                              // "\n" or "\r\n", detected at load
  final_newline: true }                                   // restored on save
```

- `line(buf, i)` walks the chunk index to the chunk holding line `i`; with ~400 chunks for 100 K lines a cached prefix count makes it a binary search. `parse` reads `buf.chunks` directly (CED16v4); nothing flattens the buffer.
- A chunk that grows past `2 × CHUNK` splits and one that falls under `CHUNK / 4` merges with a neighbor. Both happen only at chunks an edit rebuilt, and the boundary cache mirrors them (CED16v4).
- Edits are `{from: {line, col}, to: {line, col}, insert: [lines]}` deltas. `apply(buf, delta)` splices the affected line range: at most two chunks are rebuilt plus the chunk index, everything else is shared. Every delta's inverse is computed at apply time (`{from, to', insert: removed}`).
- Load: `input(path, 'text')` → split on `\n`, strip `\r`, record `eol`/`final_newline`, chunk. Save: `export_text` joins with `eol` and re-appends the final newline; `check_roundtrip` is always clean (no normalization). The session's `is_dirty` compares documents with `!=`; for the source surface the document is `{buf}` and dirty is `buf.version != saved_version` — a structural compare over 100 K lines per render would not do.

### 5.2 History

- `history = {undo: [{delta_inverse, sel_before}…], redo: […]}`. Because buffer versions share chunks, keeping the inverse delta rather than the whole prior value is about memory discipline for long sessions, not necessity; either is cheap.
- Grouping: consecutive single-character inserts merge until a word boundary, a 500 ms gap or a selection jump — the rich editor's feel. (As built since P4: the gap is measured with the events' `time_stamp`, and a caret move by key, click or gutter closes the step.)
- Position mapping through deltas (`map_pos(pos, delta)`) serves the selection, the highlight cache and later search marks; same shape as the rich editor's step mapping so the vocabulary stays familiar.

### 5.3 Guardrails

- Lines longer than 10 K code points are stored as-is but rendered plain and not passed to the walker (one `text` run); the window parse still sees them. (Built in P4: `LONG_LINE` in `source_highlight.ls`.)
- Binary content (NUL bytes) is refused at open with a diagnostic, like a document the rich surfaces cannot keep. (Not built: `input(path, 'text')` stops at the first NUL byte, so the surface cannot see one, and saving would silently truncate the file. Which layer refuses is OQ18.)

---

## 6. Virtual viewport and rendering (CED13, CED14v2)

### 6.1 Window and slots

- `geom = {viewport_h, line_h, char_w, gutter_w}` is measured on mount and on `resize`/font change through the DOM geometry waist, never during a render (no on-read layout flush). `slots = ceil(viewport_h / line_h) + OVERSCAN` (overscan 8 rows each side).
- The template renders `for (k in 0 to slots - 1)` rows; row `k` shows line `top_line + k` or is empty past EOF. A row is `<div class:"src-row" <span class:"src-gutter" n> <span class:"src-text" …>>`; the text span's children are either one plain string or token spans, provisional or exact (§6.5). Positional reconciliation updates only rows whose content changed.
- Gutter width is `digits(count) × char_w + padding`, so it changes only when the line count crosses a power of ten.

### 6.2 Scrolling

- `wheel`: `top_line += round(deltaY / line_h)` (accumulating fractional remainders), clamped to `[0, count − visible]`; `'prevent-default'`.
- Own scrollbar: a track element with a thumb positioned at `top_line / count` and sized `max(min_thumb, visible / count)`; `mousedown` on the thumb starts a drag gesture in state, `mousemove` maps the pointer delta to lines, `mouseup` ends it (the drawing surface's gesture pattern). Click on the track pages.
- Keyboard: PageUp/PageDown move the caret a page and the window follows; Home/End/arrows move the caret; `ensure_visible(sel.head)` adjusts `top_line` after every caret change.
- Horizontal: `left_col` in state, applied as a `transform`/negative margin on the text column; horizontal wheel and caret-follow adjust it. No horizontal virtualization (lines are rendered whole). (As built since P4: `left` columns in the view, a negative `margin-left` on the surface, which slides under the gutter, painted above it with `z-index`; the measured width subtracts the hidden columns; caret-follow keeps a few columns of context; wrapping resets `left` to 0.)

### 6.3 Geometry for mouse and caret

- Native hit-testing inside the `contenteditable` host resolves a click to a text-node boundary in a slot; `selectionchange` reports it and the surface maps `(slot, offset) → {line, col}`. No script-side x→column math is needed for clicks on text.
- Clicks past the end of a line or on the gutter are handled by `mousedown` with `char_w` arithmetic (monospace; tabs expand by `tab_size`); wide glyphs are rare enough in source that v1 accepts the approximation, with real measurement (`dom_rect_list` of the row) as the fallback when a row contains non-ASCII. (As built since P4: native hit-testing already puts a click past a line's end at its end, so no arithmetic is needed; a press on a line number selects the line, and Shift extends the selection to it, through `data-line` on the number.)

### 6.4 Why editor-owned scrolling unlocks soft wrap later

With `top_line` (and later `{top_line, top_row}`) as the scroll position, wrapping a line only changes how many rows a slot consumes: the view renders slots until the viewport is filled, measures nothing globally, and the scrollbar maps `top_line / count` as before (an approximation CodeMirror also accepts for unmeasured regions). A spacer-based design would need per-line heights for the whole file. Soft wrap is therefore P3+ work local to `source_view.ls`, not a redesign.

**As built (P3).** The view is one state record `{handle, top, row, rows, cols, wrap}` in `source.ls`: `row` is how many rows of line `top` lie above the surface, `cols` the text width in characters, measured once on mount from a hidden span's advance. Alt+Z toggles wrap, as in VS Code. Wrapped rows break at any character (`word-break: break-all`), so a line's rows are its tab-expanded length divided by `cols` with no layout read; the gutter gives a wrapped line's number the same height, and a negative `margin-top` on the text and gutter hides the top line's first `row` rows. The wheel and the track page by rows, so a line taller than the view scrolls through; the thumb scrolls to a line. Since P4, Up, Down, PageUp and PageDown move by rows while wrapping: the motion keeps the x it started from in display columns within a row, and stops before the wrap point on a row other than its line's last. The surface measures itself again on the window's `resize` event, so `rows` and `cols` follow the window. Word-boundary wrapping is not built, by decision (OQ17): the engine breaks `pre-wrap` text by the full UAX #14 rules, so the script cannot compute those rows; they would have to be measured, and character wrap keeps the row arithmetic exact.

### 6.5 Plain, provisional and exact rows (CED14v2)

State carries `hl = {version, first, last, runs, exact}`, where `runs[i]` holds the token runs for line `first + i` (null for a line with no runs yet) and `exact` is false while the runs are provisional. The row renderer asks `spans_for(hl, line)`: the runs when `first <= line <= last` and the entry is not null, otherwise null, which renders plain. There is no version comparison in the renderer, because every edit maps `hl` forward and `hl` always describes the current buffer.

| Row state | When | Renders |
|---|---|---|
| Plain | the line lies outside `[hl.first, hl.last]`: on open, after a scroll into new territory, or for lines a multi-line paste inserted | the line text, no spans |
| Provisional | the line was highlighted and an edit touched or moved it | runs mapped through the edit |
| Exact | after a pass 2 swap | runs from the parser's tree |

**Span mapping.** `map_runs(hl, delta)` is a pure function in `source_highlight.ls` and reuses the buffer's position mapping (§5.2). Columns are code points (CED21).

- Lines outside the edited range keep their runs. Lines below the edit shift by the delta's line change, and `first`/`last` shift with them.
- An insert inside one line at column `c` grows the run that contains `c`. At a boundary between two runs, the run that ends at `c` grows: left affinity, because that is the token being typed. Runs after `c` shift right. An insert in plain text stays plain.
- A delete inside one line clips the runs to the removed range and shifts the rest left; empty runs are dropped.
- A newline at `c` splits the line's runs at `c`. A run that spans `c` becomes two runs of the same class, so a newline inside a fenced code body keeps both halves `tok-code`. The right-hand runs move to the new line, starting at column 0.
- A multi-line insert, such as a paste, extends the first line by left affinity, gives the last inserted line the runs that were right of `c`, and leaves the fully inserted middle lines null, so they render plain until the swap.
- A multi-line delete joins the first line's runs left of `from.col` with the last line's runs right of `to.col`.
- Undo and redo apply their deltas through the same function. The composing IME row is excluded from mapping and reconciliation until `compositionend`, as before.

**Pass 2 and the swap.**

- `source_frame`, the `dom.request_frame` event, computes the window `[max(1, top_line − PAD), min(count, top_line + slots + PAD)]` with `PAD = 2 × slots`, so ordinary scrolling stays inside the highlighted range. It parses, walks, and assigns `hl = {version: buf.version, first, last, runs, exact: true}` together with the refreshed `scan` in one state assignment. (As built, P4: every handler that maps runs, scrolls or opens a find match calls `request_exact`, which requests the frame on the surface's `<body>` only when the rendered rows are not exact. Opening a file highlights its first window at once instead: at the top of the file the parse starts at line 0 and costs one pass 2. Two Radiant fixes made this work: a reactive re-render replaces the template's `<body>`, so a pending frame request now follows its owner to the structurally corresponding replacement (RAD_16 §8); and `lambda edit` pages now receive their viewer's host config, without which a re-render outside native input was dropped.)
- The reactive engine renders and paints one assignment in one frame, so the swap is atomic on screen: no frame mixes rows from before and after it.
- Pass 2 runs off the keystroke path. A frame handler runs between input events, so the buffer cannot change while it parses, and its result always matches the version it read. Results still carry `version`. If a later runtime delivers a result for an older version, for example from a concurrent task or a parse sliced across frames, the result is discarded and a frame re-requested. The provisional runs stay on screen meanwhile, so discarding is invisible. Mapping a stale result forward through the deltas since its version is a possible later refinement.
- One frame request is outstanding at a time (`frame_token` in state, as `slide/live.ls` does), so a burst of keystrokes costs at most one parse per frame.
- A scroll that stays inside `[hl.first, hl.last]` renders highlighted with no parse. A scroll beyond it renders the new rows plain, requests a frame, and the next swap replaces `hl` with a window centered on the new viewport.

---

## 7. Highlighting pipeline (CED15v2)

### 7.1 Walker contract

```lambda no-run
// no-run: contract sketch
// spans: the flat span records parse() returned (CED15v2, CED16v4); window: [first, last] 0-based
// result: array indexed by (line - first) of [{s, e, c}] runs in code-point columns, sorted, non-overlapping
fn walk(spans, lines, window) => …
```

- Covering-span rule: a span's construct gets its class; spans inside it paint over it in their own columns; text outside every span renders without a class.
- Multi-line spans are clipped per line. The parser already reports code-point columns (CED21), so the walker does no byte conversion.
- Walkers are pure functions of `(spans, lines, window)`; they never see the DOM.
- As built, `line_runs` in `source_highlight.ls` paints a line in three layers: the leaf block covering it (heading, code, table, link definition), then the markers of the containers around it, outermost first (each container's marker starts where the enclosing one's ended, so a list in a quote colors both), then inline constructs, outermost first. A leaf block nested in a container starts at its own column on its first line and after the markers and indentation on its later lines. Lines over `LONG_LINE` (10,000 code points) get no runs (§5.3).

### 7.2 Markdown walker

| Node | Class | Note |
|------|-------|------|
| `h1…h6` | `tok-heading` (+ `tok-heading-N`) | ATX marker and setext underline included by the span |
| `p` | — | text stays `tok-text` |
| `blockquote` | `tok-quote` on the marker columns only | marker from the column map; content classes come from children |
| `ul`/`ol`/`li` | `tok-list-marker` on the marker columns | same |
| `code` (block) | `tok-fence` for fence lines, `tok-code` body | body is one run in the POC (no delegation) |
| `pre` indented | `tok-code` | |
| `hr` | `tok-hr` | |
| `table` | `tok-table-delim` on pipes/alignment row | cell content from children |
| `em`/`strong`/`del` | `tok-emphasis`/`tok-strong`/`tok-strike` | delimiters covered |
| `code` (inline) | `tok-code` | |
| `a`/`img` | `tok-link` for the text, `tok-url` for `(…)`/`<…>`, `tok-ref` for `[id]` | sub-spans emitted by the parser as attributes `textpos`/`urlpos` |
| link definition | `tok-ref` + `tok-url` | |
| `math` | `tok-math` | |
| raw HTML block / inline | `tok-html` | POC: one class; P3 may hand the span to the HTML walker |
| front matter | `tok-meta` | |

### 7.3 HTML walker

| Span kind (CED18v2) | Class |
|--------------|-------|
| `tag` | `tok-tag-punct`: painted first, so what the inner spans leave uncovered (`<`, `</`, `>`, `/>`, `=`, the spaces between attributes) keeps it |
| `tag-name` | `tok-tag` |
| `attr-name` | `tok-attr` |
| `attr-value` | `tok-attr-value` (quotes included) |
| `comment` (including `<![CDATA[…]]>` outside foreign content, which HTML parses as a bogus comment) | `tok-comment` |
| `doctype` | `tok-doctype` |
| `raw` (`script`/`style`/`textarea`/`title` bodies) | `tok-raw` (POC); later delegate `script`→JS, `style`→CSS when walkers exist |
| `entity` | `tok-entity` |
| text | no class |

### 7.4 Registry

`source_highlight.ls` exports `walker_for(type) → fn | null` with `{markdown: md_walk, html: html_walk}`; the descriptor maps suffix → parse `type`. A format with a parser but no walker (`json`, `css`, `lambda`, …) opens, edits and saves, and renders plain; adding a walker later needs no change outside the registry and (when spans are missing) that format's parser.

### 7.5 Theme

Classes are styled by a stylesheet in the surface (`source.css` string, light and dark via `prefers-color-scheme`). Rule for theme authors: a `tok-*` rule may change color, background, text-decoration and font-style, **never** font-family, font-size or font-weight, so pass 2 cannot change advances relative to pass 1 (monospace italics keep their advance; bold in some monospace fonts does not).

---

## 8. Parser enhancements — the native work (CED16v4–CED18v2)

This is the only native code the design needs. Everything is behind options; default parses stay byte-identical.

### 8.1 Markup engine (Markdown first; the mechanism is format-neutral)

1. **Chunked input, sliced.** `parse()` accepts the buffer's chunk array or a flat array of strings. For a windowed parse, `MarkupParser` builds a `char*` pointer array over only the lines from the restart line to `last + LOOKAHEAD`, and sets a `line_base` so `sourcepos` still reports absolute lines. Lookahead is a few lines, enough for setext underlines and table delimiter rows. Strings are not copied and `splitLines` is skipped, so the existing `lines[]`-indexed block parsers run unchanged. Ownership: the Lambda arrays are rooted for the call's duration; the parser never frees adopted lines.
2. **Windowed parse** (`window: [first, last]`). The boundary scan (fence open/close, HTML block open/close, front matter) starts from the nearest valid boundary in the `prescan` cache, or line 1 without one, records the state at each chunk boundary it crosses, and stops re-checking suspect entries at the first one whose recomputed state matches (CED16v4). For Markdown it also runs the parser's own link-definition pre-scan (`MarkupParser::prescanLinkDefinitions`, resumable over line ranges) chunk by chunk to the document's end, so the window parse is seeded with every chunk's labels. The updated states and labels are returned as flat values (`[kinds, spans, states, restart, labels]`). The **safe restart** is the last line `≤ first` that starts a block after a blank line, outside any fence, HTML block or front matter, or line 1. The window's text, from the restart line to `last + LOOKAHEAD`, is parsed whole on the private pool; blocks inside containers report their spans through the container's column map.
3. **Inline `sourcepos`** (`sourcepos: 'inline'`). (a) `record_source_position` is applied at every block depth, not only top level. (b) A **column map** travels with stripped content: when a container (list item, blockquote, indented/lazy continuation) re-parses stripped lines, it records, per content line, the source line and the byte offset of the stripped prefix; `parse_inline_spans` receives the map and converts the offsets it already tracks in its `StringBuf`/cursor into source line/column when it finalizes an inline element. (c) Links/images additionally record `textpos` and `urlpos`; code spans, emphasis, strikethrough, math and autolinks record the covering `sourcepos`. (d) Fenced code blocks record the fence lines separately (`fencepos` for the opening and closing lines) so the walker can color fences and body differently.
4. **MVP fallback** if (3b) proves hard for some container: the container keeps block-level `sourcepos` and its inlines carry none; the walker colors the block and leaves inline runs plain. The fidelity test reports the fallback rate on the corpus so it is visible, not silent.

### 8.2 HTML5 lexical mode

1. `html5_lex_spans(input, text, len, emit, ctx)` drives `html5_tokenize_next` with the tree builder bypassed, keeping only the tokenizer-state switching the tree builder normally performs for RCDATA (`title`, `textarea`), RAWTEXT (`style`, `script`, `xmp`, `iframe`, `noembed`, `noframes`, `noscript`) and PLAINTEXT. A token's range is the cursor before and after the call; attribute name and value ranges come from an optional state hook in `html5_switch_tokenizer_state`, null on every ordinary parse.
2. The **restart pre-scan** uses the same chunk-boundary cache (CED16v4) with the HTML restart state: inside a comment, CDATA, raw-text body or tag. It tracks `<!--`/`-->`, `<![CDATA[`/`]]>`, raw-text start/end tags and an open `<` at end of line, and finds the last line `≤ first − overscan` that starts outside all of them. The tokenizer reads contiguous text, so the slice from that line to `last + LOOKAHEAD` is joined into one buffer, which costs O(window) bytes.
3. Each token becomes one or more flat spans (CED18v2), byte ranges in the joined slice mapped back to lines through the slice's line starts, then to code-point columns as for Markdown.
4. Markdown's raw-HTML blocks can later be re-lexed by the same routine over their span (§7.2); not in the POC.

### 8.3 `parse()` option surface

```lambda no-run
// no-run: option sketch
parse(buf.chunks, {type: 'markdown', sourcepos: 'spans', window: [first, last], prescan: scan})   // block and inline spans
parse(buf.chunks, {type: 'html', sourcepos: 'spans', window: [first, last], prescan: scan})       // tokenizer spans
// both return [kinds, spans, states, restart_line, labels] (CED16v4); `states` and `labels`
// are the refreshed cache; HTML's `labels` is empty
```

- `sourcepos: true` keeps its meaning (top-level blocks, as a `sourcepos` attribute on the tree); `'spans'` selects the highlight parse and its flat result. `prescan` is optional; without it the boundary scan starts at line 1, so a one-off caller pays the linear scan once. The cache is opaque data to Lambda code: the editor stores it, realigns it after an edit (`scan_after`), and passes it back.
- The result holds no tree: every `parse()` keeps its `Input` until the runtime resets, so a tree per frame would accumulate (OQ15). The flat arrays are ordinary GC values.

### 8.4 Explicitly not done

No change to JSON, YAML, TOML, CSS, LaTeX, Lambda or any other parser; no incremental re-parse (the window parse makes it unnecessary for highlighting); no Tree-sitter binding; no `__source_line` revival (it is subsumed by `sourcepos` and can be retired when the rich HTML surface stops needing it).

---

## 9. Input seam, selection, IME, clipboard (CED19v2)

- **Host**: the slot container is `contenteditable="plaintext-only"`, `spellcheck=false`, `tabindex=0`. Rows and the gutter are `user-select: none` except the text span. The `dom` package classifies the host and emits `editaction` for every edit intent; the surface returns the edit result the way the rich surface does (`edit_result.*`).
- **Target mapping**: an `editaction` carries DOM boundaries (slot text node, offset) or the model selection the surface last projected; both map to `{line, col}` by `slot index + top_line` and CED21 conversion. A target that references a row outside the window (possible only through a stale native selection) is rejected with `decline`, the DOM selection re-projected, and the action dropped.
- **Intents** handled in P0: `insertText`, `insertLineBreak`/`insertParagraph` (both newline), `deleteContentBackward/Forward`, `deleteWordBackward/Forward`, `deleteSoftLineBackward/Forward`, `insertFromPaste` (text lines), `deleteByCut`, `insertCompositionText`, `historyUndo/Redo` (also bound on `keydown` because the rich editor found the events unreliable). `insertTranspose`, `insertFromDrop` and the `format*` family are declined.
- **Selection**: `sel = {anchor: {line, col}, head: {line, col}}` in state. Projection after every render: both ends inside the window → set the DOM range on the slot text nodes; an end outside → clamp to the window edge row. (P3: the clamped DOM range already paints every rendered row between the ends, so no `tok-selected` runs are needed. A Shift+press extends the model selection from its own anchor, which may lie outside the window, instead of the clamped DOM anchor.) `selectionchange` with native-originated positions (mouse, shift-click, double/triple click) updates `sel` through the same mapping; a `selectionchange` caused by the surface's own projection is recognized by comparing against the last projected value and ignored. (P4: Radiant dispatched the Lambda `selectionchange` on mouse-up only for a non-collapsed range, so a click that just placed the caret reached the model on the next key; it now fires for a caret too, and the surface projects the selection again after adopting it, since the status redraw replaces the rows.)
- **IME**: `compositionstart` marks the composing row; the row is excluded from reconciliation (its vnode is reused unchanged) until `compositionend` delivers the committed text as an ordinary insert. Pass 2 skips the composing row.
- **Clipboard**: copy/cut build text from `sel` over the buffer (join with `eol`); paste arrives as plain text from the gate. Column/rectangular selection is out of scope. (P3: the surface returns the text in its edit result's `clipboard_text`, and Radiant writes it, because the DOM shows only the window's part of the selection; Lambda_Design_DOM_Editable §4.5.)
- **Keyboard** (`keydown`): arrows/word/line/page/document movement with shift extension; Tab inserts `tab_size` spaces or a tab per descriptor option; Shift-Tab dedents the selected lines; Cmd/Ctrl-A select-all (model); Cmd/Ctrl-Z/Shift-Z; Cmd/Ctrl-S falls through to the shell's existing save binding; Cmd/Ctrl-F find is P3. (P3: Tab over lines and Shift+Tab re-indent whole lines as one undo step. Cmd/Ctrl+F opens a find bar over the text's top right: matches ignore case, render as runs laid over the tokens, and show "n of m"; Enter, Shift+Enter and Cmd/Ctrl+G step and wrap; Escape returns to the selected match.)

---

## 10. Product surface and shell integration (CED20)

- `lmd/package/edit/edit.ls`: `formats = [md.descriptor, html.descriptor, svg.descriptor, source.descriptor]`; `source.descriptor.suffixes` lists the plain-text suffixes; `open_document(path, options)` honors `options.source == true` by choosing `source.descriptor` regardless of suffix; `lambda/main.cpp` parses `--source` into the launch options the edit transform already receives.
- `shell.ls`: `surface_of` gains a `'source'` branch; the toolbar shows the file group (save, save-as, reload), undo/redo, a language label and line:col status; format groups for rich text are hidden for the source surface.
- Status line: `line:col`, selection size, language, EOL style, dirty flag; `window_title` as today. (Built in P4: the left side shows `Ln, Col`, the selection's size in characters within a line or in lines across several, and the last message; the right side shows the language, `LF` or `CRLF`, and `Unsaved`.)
- **Source↔Rich switch (OQ7, P3).** `edit.ls` renders one root template, `edit_doc`, which shows the rich (or drawing) surface or the source surface over the same file. Each surface offers the other view: a "Source" button and Cmd/Ctrl+/ in the rich toolbar, a "Rich" button and Cmd/Ctrl+/ in the source toolbar when the file has a rich format. A switch hands over the current text. An unsaved text stays dirty, because the new session's checkpoint is the disk text. A text the rich format cannot keep exactly stays in the source view with a status message, the rule that refuses such a file at open. The root's body is the surface's application, so both templates produce the surface's `<body>`; the render map chains them and the surface handles its events first (Lambda_Design_DOM_Dispatch, note to ES27).
- Headless: `lambda edit --source file.md --headless --event-file fixture.json` drives everything; `event_sim` assertions read row text and classes through the normal DOM assertions.

---

## 11. Testing (CED22)

1. **Lambda unit tests** (`test/lambda/edit/`, each `.ls` with its `.txt`): buffer laws (`apply ∘ inverse = id` on random deltas; chunk invariants; `lines(chunk(split(text))) == split(text)`), history grouping, column conversions on ASCII/CJK/emoji/tab lines, window math, `map_runs` (insert inside a run, at a boundary and in plain text; delete; newline split; multi-line paste and delete; undo round trip; typing inside a token yields the same runs as an exact parse), Markdown and HTML walker goldens (`runs` for fixture documents, including fences, nested lists, raw HTML, malformed tags, entities).
2. **GTest** (`test/test_input_sourcepos_gtest.cpp`): for every corpus document and a sliding set of windows — (a) every `sourcepos` substring equals the construct as written; (b) token classes from the window parse equal those from a full parse restricted to the window; (c) default `parse()` output is byte-identical with the options off; (d) the HTML lexical pass agrees with the tree parser on tag names and attribute values; (e) on random edit sequences, a parse with the `prescan` cache and suspect marking equals a parse that scans from line 1. (Built in P4, input suite. (b) compares per-line coverage by span kind, which fixes every class the walker paints; (c) compares the tree a span-recording parse builds with a plain parse's; (d) uses two crafted documents with no implied elements; (e) runs 300 random edits on 16-line chunks, mirroring `scan_after`. The corpus is `test/markdown`; the corpus cases skip where it is absent.)
3. **UI fixtures** (`test/ui/edit/source_*.json`): mount; typing; newline/delete across chunk boundaries; undo/redo; scroll by wheel, thumb and keyboard; caret-follow; selection projection at window edges; typing inside a highlighted window never renders a highlighted row plain before the swap; a scroll into new territory renders plain, then highlighted; paste; IME composition; save; the **stress fixture** — a 5 MB Markdown file generated into `./temp/` by a test-time script, sustained typing at the middle and end, page-long scrolls, with a frame-time budget asserted from the event result. (Two items are deliberately not fixtures. The headless runner drains every input turn's frames before the next step, so the provisional rows between a keystroke and its swap cannot be observed there; `map_runs` and the unit test "typing inside a token" cover them. A frame-time budget would be a wall-clock assertion in a debug build run six fixtures at a time, which is flaky and against the release-only timing rule; pass 2 is timed in a release build instead, impl record §8.2.)
4. **Doc gate**: `lambda` fences in this and the user docs compile (`make check-doc-code`); the `no-run` sketches above carry their reason.

---

## 12. Milestones

| Milestone | Delivers | Exit gate |
|-----------|----------|-----------|
| **P0 — plain virtualized editor** | `source` descriptor + `--source`; buffer/history; window, slots, own scrollbar; `editaction`/`selectionchange`/`keydown` seam; save/reload; stress fixture | Edit, scroll and save a 5 MB file headlessly inside the frame budget; buffer and conversion unit tests green; `make test-lambda-baseline` and `make test-radiant-baseline` unchanged |
| **P1 — Markdown highlighting** | Chunked `parse()` input, windowed parse with the chunk-boundary restart cache, inline `sourcepos` with column map, Markdown walker, plain-first render with provisional span mapping and the atomic swap, theme | Fidelity and cache-equivalence GTests green on the Markdown corpus with fallback rate reported; walker and `map_runs` goldens; typing latency unchanged versus P0; edits in a highlighted window never show plain rows; pass 2 cost is the same with the caret at line 1 and at the end of the stress file |
| **P2 — HTML highlighting** | HTML lexical mode with window over the shared restart cache; HTML walker; entities, raw-text bodies | Lexical/tree agreement test; walker goldens; mixed `.html` with scripts and comments highlighted in the headless fixture |
| **P3 — parity and polish** | IME and clipboard fixtures, selection runs for off-window rows, find, indent/dedent, soft wrap (`{top_line, top_row}`), Source↔Rich toggle per OQ7 | Fixture suite green; wrap fixture; no regression in P0 stress budget. Built 2026-10-07; container column maps (CED17, OQ10) were added here too. |
| **P4 — outstanding items** | Pass 2 on the next frame (OQ16); reference links resolved across the window (CED16v4); blocks nested in containers; the long-line guard (§5.3); the status line (§10); row-wise caret motion and re-measuring on resize (§6.4); horizontal scrolling (§6.2); gutter clicks (§6.3); undo groups by pause and caret move (§5.2); `source_units.ls` (CED21); the GTest ring (§11) | Built 2026-10-07: the §11 GTest green with 0 of 1,577 corpus windows differing from the full parse; edit fixtures 42/44, the two failures fail identically before P4; pass 2 about 6.4 ms with the cache in a release build. OQ17 decided (character wrap stays); OQ18 open |
| **Later** | Walkers for `json`/`css`/`lambda`/`yaml` (parser spans as needed); fence and raw-HTML delegation; `load_text_doc` reuse; preview/sync per the CodePad plan over the same spans | separate decisions |

---

## 13. Risks and mitigations

- **Interpreter-side cost per keystroke** (reactive re-render of ~60 rows, chunk rebuild, selection projection). *Mitigation:* the stress fixture is P0's first deliverable; row count is bounded; profiling with `JS_EXEC_PROFILE`-style counters is not available for Lambda, so the fixture asserts wall time from the event result.
- **Column-map fidelity** for inlines inside containers (CED17). *Mitigation:* the fallback is explicit and measured; the GTest reports it per corpus file.
- **Window-parse divergence** at restart points (lists across blank lines, lazy continuation, setext underlines). *Mitigation:* restart only on blank lines outside fences; the contract is token-class equality, tested by the sliding-window GTest; a reported divergence class gets a targeted restart rule, not a wider overscan.
- **Provisional spans are wrong for a frame** when an edit changes token structure, such as typing the backtick that opens a code span. *Mitigation:* left affinity makes ordinary in-token typing right; the swap corrects the rest within one frame; the `map_runs` golden tests pin the rules.
- **Restart-state cache drift** after edits, splits or merges. *Mitigation:* suspect marking instead of trusting later entries; the cache-equivalence GTest compares against a scan from line 1 on random edit sequences.
- **Selection projection at window edges** and **IME × reconciliation** (as in the CodePad analysis). *Mitigation:* model-owned selection with clamped projection and dedicated fixtures in P0; composing row frozen until `compositionend`.
- **`sourcepos` growth in the rich surfaces**: `markdown.ls` already reads `sourcepos`; adding nested/inline positions must not change its view-only logic. *Mitigation:* `sourcepos: true` keeps today's semantics; the new level is opt-in (`'inline'`).
- **Scope creep toward an IDE.** *Mitigation:* §1 non-goals are normative; new features need a new CED entry.

---

## 14. Open questions — resolved (user, 2026-10-06)

All eight questions were put to the user, who took the recommendation on each. The rulings are folded into the ledger rows they affect; this section keeps the question and the answer.

| ID | Question | Decision |
|----|----------|----------|
| **OQ6** | Lambda surface in `lmd/package/edit`, retiring the CodePad JS/TS plan (CED1, CED10)? | **Yes.** CED11 stands; the CodePad doc keeps only its preview/sync plan (§7 there) as forward work. |
| **OQ7** | `lambda edit --source file.md` now, Source↔Rich toggle later, or the toggle from the start? | **Flag now**; the toggle is P3 at the earliest (CED20). |
| **OQ8** | Editor-owned scrolling or a native scroll container with a spacer? | **Editor-owned** (CED13). |
| **OQ9** | May `parse()` accept an array of lines? | **Yes** (CED16, now CED16v4, which also accepts the chunk array); the editor never joins the buffer for highlighting. |
| **OQ10** | Accept the block-level fallback inside containers whose inline spans are not yet column-mapped? | **Accepted for P1** with the fallback rate reported by the fidelity test; per-container fixes in P3 (CED17). |
| **OQ11** | Soft wrap in P1 or P3? | **P3**, on the `{top_line, top_row}` scroll model (§6.4). |
| **OQ12** | Fence bodies as one `tok-code` run in the POC? | **Yes**; per-language delegation is later work (§7.2). |
| **OQ13** | Continue the `CED#` series here, or fold both documents? | **Keep both**: this document is the design of record for the editor surface, `Radiant_Code_Editor.md` for preview/sync, Appendix B is the bridge. |

OQ6–OQ13 left no decision blocking P0 (§12). Items to verify during P0 rather than decide now: which unit the `editaction` target range reports (CED21 conversion), and the exact geometry waist call for viewport height and character advance (§6.1).

### 14.1 Raised during P0 — resolved (user, 2026-10-06)

- **OQ14 — wheel and thumb-drag scrolling for an editor-owned viewport (CED13).** The DOM layer's hot-path guard kept `wheel`, `scroll` and `mousemove` away from every Lambda author template, so the surface could scroll only by keyboard and track clicks. **Decision:** Radiant delivers continuous events to author templates that declare them, ruled as ES23v2 in `vibe/Lambda_Design_DOM_Dispatch.md` (an exact per-event registry flag keeps documents that declare none at zero cost; package loading and behavior dispatch stay guarded). The surface now handles `on wheel` (pixels accumulated into whole lines) and drags the thumb with `on mousemove`.

### 14.2 Raised during P1 — resolved (user, 2026-10-07)

- **OQ15 — flat spans instead of a trimmed tree.** Every `parse()` keeps its `Input` until the runtime resets, so a trimmed tree per frame would accumulate memory. **Decision:** keep the flat span records; CED15 becomes CED15v2 and CED16v2 becomes CED16v3 (old text in Appendix C).
- CED18 is revised to **CED18v2** as a consequence of OQ15: the HTML tokenizer reports the same flat spans instead of an `<html-lexical>` tree.
- **OQ16 — frame request for pass 2 (CED14v2).** **Decision:** wait for the DOM layer's `dom.request_frame`, in progress outside this branch. Until it lands, pass 2 runs before the handler returns (about 4 ms per change); `settle` in `source.ls` is the one place that changes. (Done in P4, once `dom.request_frame` reached master: §6.5.)

### 14.3 Raised during P4

- **OQ17 — word-boundary soft wrap.** P3 wraps at any character, so a line's rows are its display width over `cols` and the view's row arithmetic is exact. Radiant breaks `pre-wrap` text by the full UAX #14 rules (spaces, hyphens, BA characters, CJK, automatic hyphenation), which the script cannot reproduce. Options: (a) keep character wrap; (b) word wrap with measured rows: after each render read the rendered rows' heights (and, for caret motion, each row's break offsets) and use them for the lines in the window, estimating the rest, as CodeMirror does for unmeasured regions; (c) word wrap with a script line breaker that matches the engine for ASCII spaces only, accepting row errors elsewhere. Recommendation: (b), behind the current Alt+Z toggle, if word wrap is wanted at all. **Decision (user, 2026-10-07): (a), keep character wrap.**
- **OQ18 — files with NUL bytes (§5.3).** `input(path, 'text')` stops at the first NUL byte (`input_from_local_path` reads with `strlen`), so the surface sees a truncated text and a save would write it back truncated. Options: (a) `input(path, 'text')` raises an error for a file with a NUL byte, and `lambda edit` reports it at open; (b) text input keeps every byte, NUL included, and the source descriptor refuses a text containing one; (c) leave `input` alone and add a separate probe. (a) and (b) change `input()` for every caller and need a ruling (no `S#` covers NUL bytes in text input). Recommendation: (a). **Open: the user left it for later (2026-10-07).**

---

## Appendix A — Implementation touchpoints (as built, P0–P4)

The proposal's touchpoints changed during P1 (OQ15: flat spans, no trimmed tree); this table lists what was built.

| Location / symbol | Change |
|---|---|
| `lambda/runtime/lambda-eval.cpp` `fn_parse_highlight_spans` | `parse(src, {type, sourcepos: 'spans', window, prescan})`: string, line-array or chunk-array input read in place; `[kinds, spans, states, restart_line, labels]` out (CED16v4) |
| `lambda/input/markup/markup_highlight.{hpp,cpp}` | the boundary scan with its per-chunk cache and suspect convergence (`extend_states`), per-format restart rules, the per-chunk link-label scan, the span sink and container column maps (CED17), the window parse on a private pool |
| `lambda/input/markup/markup_parser.{hpp,cpp}` `prescanLinkDefinitions` | the link-definition pre-scan as a method resumable over line ranges (`LinkPrescanState`), shared by full parses and the label scan |
| `lambda/input/markup/block/block_document.cpp`, `block_quote.cpp`, `block_list.cpp` | block spans at every depth through `highlight_note_item`; containers register their stripped lines with the sink |
| `lambda/input/markup/inline/*.cpp` | inline spans from the inline parsers, mapped back through the block's segments |
| `lambda/input/html5/html5_tokenizer.cpp`, `html5_parser.h` | `html5_lex_spans`: the tokenizer with the tree builder bypassed and a state hook for attribute spans (CED18v2) |
| `radiant/event.cpp` `radiant_frame_requests_follow_rebuild`, `radiant/cmd_layout.cpp` | a pending frame request follows its owner across a reactive rebuild (P4) |
| `radiant/window.cpp` | transformed (`lambda edit`) pages receive the viewer's host config (P4) |
| `lmd/package/edit/edit.ls`, `shell.ls`, `lambda/main.cpp` edit options | `source.descriptor`, `--source`, the Source↔Rich root `edit_doc` |
| `lmd/package/edit/source.ls`, `source_buffer.ls`, `source_highlight.ls`, `source_units.ls` | the surface (window math lives in `source.ls`; no separate `source_view.ls`), the buffer, the walker and cache, the column units |
| `test/lambda/edit/source_*.ls` + `.txt`, `test/test_input_sourcepos_gtest.cpp`, `test/ui/edit/edit_src_*.json` | unit, GTest and UI rings (§11) |

## Appendix B — Relation to the CodePad ledger (CED1–CED10)

| CodePad ruling | Fate here |
|---|---|
| CED1 (JS plain-DOM editor; no CM6) | **Superseded by CED11** (Lambda surface). "No CM6, no native editor" stands. |
| CED2 (script-owned buffer; DOM is a render target) | Kept; refined by CED12 (chunked persistent list, immutable values). |
| CED3 (virtualized viewport, mandatory) | Kept; refined by CED13 (editor-owned scrolling, no spacer). The 500-wrap-cap premise is stale (§2.3) but the conclusion holds. |
| CED4 (model selection is truth; native single range as projection) | Kept unchanged (CED19v2). |
| CED5 (stream tokenizers, no regex) | **Superseded by CED15** (parser-driven). "No regex, no Tree-sitter vendoring" stands. |
| CED6–CED8 (preview, parser source spans, no two-way editing) | Kept as the later plan; CED16v4–CED18v2 deliver the span mechanism CED7 asked for, at finer grain. |
| CED9 (`lambda.exe edit` as a separate bundled app) | **Superseded by CED20** (a surface of the existing `lambda edit`). |
| CED10 (portable TS, Lambda twin later) | **Superseded by CED11**; no twin. |

## Appendix C — Superseded rulings

Rulings replaced in place keep their text here, struck through, per `doc/Doc_Convention.md` §4.

### CED14 — superseded 2026-10-06 by **CED14v2**

~~**Two-pass rendering.** Pass 1: the handler that changes buffer, selection or `top_line` re-renders synchronously with every dirty slot as **plain text** and requests a frame. Pass 2: the frame handler parses the window, stores token runs in state (`hl = {version, first, last, runs}`), and the re-render decorates slots with spans. A slot shows highlight only when `hl.version == buffer.version` and its line is in `[hl.first, hl.last]`; otherwise plain.~~

~~Typing latency is pass 1 only; the parser is never on the keystroke path. Plain and highlighted passes produce identical glyph runs (same font, theme must not change advances — §7.5), so the overlay never shifts text. Scrolling within an already highlighted window costs no parse. ~~

*Replaced because* every edit bumped the buffer version, so every visible row dropped to plain text until the next frame: a flicker on each keystroke. CED14v2 keeps the colors by mapping the runs through the edit and swapping in exact runs on the next frame.

### CED16 — superseded 2026-10-06 by **CED16v2**

~~**Windowed parse contract.** `parse(lines, {type, sourcepos, window: [first, last]})` where `lines` may be an **array of strings** (one per line, no terminators) and `window` is 1-based inclusive like `sourcepos`. The parser (a) pre-scans all lines cheaply for restart state (fences, HTML blocks, raw-text elements, comments) without allocating; (b) picks the **safe restart line** at or before `first − overscan`; (c) builds nodes only for blocks/tokens intersecting the window and stops after `last`; (d) disables reference resolution. Highlight-grade fidelity is the contract: token classes inside the window must equal those of a full parse; structural differences outside (a list split in two) are allowed.~~

~~Joining 5 MB per keystroke and `splitLines`'s per-line `malloc` are the real costs, not block parsing; the line array removes both (Lambda strings are NUL-terminated, so `char**` is zero-copy). A forward pre-scan is O(file) in bytes with no allocation (≈1–2 ms at 5 MB) and is the only way to know fence state — backward scanning is ambiguous. Per-chunk caching of pre-scan state is a later optimization, not a P1 need. The line-array input shape was confirmed 2026-10-06 (OQ9). ~~

*Replaced because* pass 2 flattened the buffer and pre-scanned from line 1 on every frame, so its cost grew with file size and with the caret's position. CED16v2 reads the chunks in place, slices only the window, and caches restart state at chunk boundaries.

### CED15 — superseded 2026-10-07 by **CED15v2**

~~**Highlighting = parser-driven.** A format's highlighter is a *span walker* `walk(tree, window) → [per-line [{col_start, col_end, class}]]` over the trimmed tree the parser returns; coloring is CSS classes (`tok-*`). No stream tokenizers (supersedes CED5), no regex, no Tree-sitter, no second grammar for any format. Formats without a walker render plain.~~

*Replaced because* a trimmed Mark tree per frame would live in a retained `Input` (OQ15); the walker reads flat span records instead.

### CED18 — superseded 2026-10-07 by **CED18v2**

~~**HTML uses a source-ordered lexical tree.** `parse(lines, {type: 'html', lexical: true, window})` runs the **tokenizer only** and returns a flat `<html-lexical>` of `start-tag` (with `attr` children carrying `sourcepos` and `valuepos`), `end-tag`, `text`, `comment`, `doctype`, `cdata` nodes, each with `sourcepos`; raw-text (`script`/`style`) bodies are one `raw` node. The DOM tree builder is not run.~~

*Replaced because* the window parse returns flat span records for every format (OQ15, CED15v2); HTML uses the same result and the `sourcepos: 'spans'` option rather than a `lexical` flag.

### CED16v2 — superseded 2026-10-07 by **CED16v3**

~~**Windowed parse over the chunked buffer, with cached restart state.** `parse(chunks, {type, sourcepos, window: [first, last], prescan})`. (a) **Input** is the buffer's chunk array (arrays of lines, no terminators) or a flat line array, read in place: no join, no string copy; a windowed parse only touches the lines from its restart line to `last + LOOKAHEAD`. (b) **Restart state** is the format's small data value for constructs that cross lines (fence, HTML block, front matter; for HTML: comment, CDATA, raw-text body, open tag). The parser returns the state at the start of every chunk it scanned as a `prescan` attribute on the returned root, and accepts that list back through the `prescan` option, so it scans only from the nearest valid cached chunk boundary to the window instead of from line 1. (c) The editor keeps `scan = {states, suspect_from}` beside the buffer. An edit marks every entry after the first chunk it rebuilt as **suspect**; a chunk split or merge is mirrored in the list so later entries stay aligned. When the parser recomputes a suspect boundary state and finds it equal to the cached one, that entry and all later ones become valid again, and the parser jumps to the nearest valid boundary before the window. (d) As before: the **safe restart line** is at or before `first − overscan`; nodes are built only for blocks and tokens intersecting the window; parsing stops after `last`; reference resolution is off. Highlight-grade fidelity is the contract: token classes inside the window equal those of a full parse; structural differences outside it are allowed.~~

*Replaced because* the parse returns flat span records on the GC heap rather than a root carrying a `prescan` attribute (OQ15).

### CED16v3 — superseded 2026-10-07 by **CED16v4**

~~**Windowed parse over the chunked buffer, with cached restart state.** `parse(chunks, {type, sourcepos: 'spans', window: [first, last], prescan: {states, valid}})` returns `[kinds, spans, states, restart]`: span kinds as symbols, spans as flat ints (kind, block, line, col, end line, end col; 0-based lines, code-point columns), the restart state per chunk boundary as flat ints, and the line the parse began. The parse runs on a private pool released before returning (CED15v2). (a) **Input** is the buffer's chunk array (arrays of lines, no terminators) or a flat line array, read in place: no join, no string copy; a windowed parse only touches the lines from its restart line to `last + LOOKAHEAD`. (b) **Restart state** is the format's small data value for constructs that cross lines (fence, HTML block, front matter; for HTML: comment, CDATA, raw-text body, open tag). The parser returns the state at the start of every chunk it scanned and accepts that list back through the `prescan` option, so it scans only from the nearest valid cached chunk boundary to the window instead of from line 1. (c) The editor keeps `scan = {states, suspect_from}` beside the buffer. An edit marks every entry after the first chunk it rebuilt as **suspect**; a chunk split or merge is mirrored in the list so later entries stay aligned. When the parser recomputes a suspect boundary state and finds it equal to the cached one, that entry and all later ones become valid again, and the parser jumps to the nearest valid boundary before the window. (d) As before: the **safe restart line** is at or before `first − overscan`; nodes are built only for blocks and tokens intersecting the window; parsing stops after `last`; reference resolution is off. Highlight-grade fidelity is the contract: token classes inside the window equal those of a full parse; structural differences outside it are allowed.~~

*Replaced because* a reference link whose definition lay outside the window stayed plain (the one fidelity gap P3 measured, 0.28% of corpus lines), and a boundary a chunk split created was filled with the default state, so it could match a recomputed state falsely and leave later boundaries stale.

### CED19 — superseded 2026-10-07 by **CED19v2**

~~**Editing seam reuses the `lambda.edit` pipeline.** The window host is `contenteditable="plaintext-only"`; the surface handles `editaction` (insertText, insertLineBreak/insertParagraph, delete*, insertFromPaste, insertCompositionText, historyUndo/Redo) by mapping the event's DOM target range to `{line, col}` through the slot index, editing the buffer, and restoring the DOM selection from the model. Model selection is truth; the DOM range is its projection clamped to the window (CED4 kept). `keydown` owns navigation, select-all, undo/redo, save. Mouse → caret stays native inside the host and comes back through `selectionchange`. IME: the composing slot is not re-rendered until `compositionend`.~~

*Replaced because* the model-surface binding accepts only rich hosts; the surface declines rich intents instead, which gives the same plain-text behavior (impl record §1.1).
