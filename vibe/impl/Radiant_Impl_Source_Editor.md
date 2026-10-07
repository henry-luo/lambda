# Radiant Source Editor — Implementation Plan and Record

**Date:** 2026-10-06
**Status:** P0 (`9007c54b3`), P1 Markdown highlighting (`8c477c3a8`), P2 HTML highlighting (`1ca09a3d5`, §6) and P3 parity and polish (`c9adc7199`, §7) are on master. P4, the outstanding items, is on branch `worktree-source-editor-p4` (§8). OQ15 and OQ16 were decided by the user on 2026-10-07 (§5.5); in P4 the user decided OQ17 (soft wrap stays character wrap) and left OQ18 open (design §14.3).
**Design:** [Radiant Source Editor](../radiant/Radiant_Design_Source_Editor.md) (CED11–CED22, CED14v2, CED15v2, CED16v4, CED18v2, CED19v2).

## 1. What P0 has built

| Layer | Owns | Files |
|---|---|---|
| Buffer (CED12) | chunked immutable line list, deltas and inverses, chunk split/merge, exact text round trip | `lmd/package/edit/source_buffer.ls` |
| Source surface (CED11, CED13, CED19) | descriptor, `edit <source_app>` template, window projection, selection mapping, intents, history, keyboard navigation, IME, track paging | `lmd/package/edit/source.ls` |
| Shared file half | file label, window sync, Save, file dialogs and their actions, dialog CSS | `lmd/package/edit/files.ls` (moved out of `shell.ls`) |
| Registry (CED20) | `source.descriptor` for plain-text suffixes; `options.source` picks it for any file | `lmd/package/edit/edit.ls` |
| Session | `same_doc` format hook so dirty state compares buffer versions | `lmd/package/edit/session.ls` |
| Toolbar | history buttons accept the source surface's `{kind: 'source', can_undo, can_redo}` | `lmd/package/edit/toolbar.ls` |
| CLI | `lambda edit --source <file>` → transform option `source: true` | `lambda/main.cpp`, `radiant/window.cpp`, `radiant/radiant.hpp` |
| Focus | focus survives reactive regeneration for any focused element, not only text controls | `radiant/cmd_layout.cpp` |
| UI runner | fixture keys `source` (passes `--source`) and `prepare` (argv run before launch to generate a `temp/` document) | `test/test_ui_automation_gtest.cpp` |
| Continuous events (ES23v2) | `wheel`, `mousemove`, `pointermove`, `scroll`, `dragmove`, `dragover` reach author templates that declare them, behind an exact per-event registry flag; one shared continuous-event list; wheel records carry `deltaX`/`deltaY` | `lambda/runtime/template_registry.{h,cpp}`, `radiant/event.cpp` |

### 1.1 How the window maps to the selection bridge

The window is projected as a lambda.editor-shaped document, `node('doc', [node('src_line', [text(line)]) …])`, and applied through the edit package's existing `view map` template (`rich_text.ls`). That makes `render_map_maybe_set_source_doc_root` treat it as the editor document root, so every DOM boundary in row `k` maps to source path `[k, 0]` plus a UTF-8 byte offset, and `finish_model_edit` / an `editaction` result projects a model selection back after the render. Byte offsets convert to code-point columns per row (CED21). No native change was needed for the bridge.

The host is `contenteditable="true"`, not `plaintext-only` as CED19 says: `radiant_model_edit_surface_bind` accepts only rich surfaces. The surface declines every rich-only intent, so behavior is plaintext; paste reads `evt.data`.

### 1.2 Findings worth keeping

- **Focus was lost on every reactive render** for any focused element other than a text control. `capture_lambda_focus_restore` recorded a restore path only for `<input>`/`<textarea>`. After the first edit the host was no longer focused, and Tab, which the engine sends only to the focused element, fell through to sequential focus. Fixed at the root: any focused element is restored by its template path, checked by tag and id, with a by-id fallback.
- **A module-level `let` used before its definition evaluates to nothing.** `source.ls` referenced `css` in `page()` above `let css = …`; the stylesheet silently vanished. Defining it first fixed it. Worth a compile-time diagnostic (not filed yet).
- **`member(list, x)` is not a builtin**; `lambda.edit.model` defines it. Use `contains(list, x)` outside that module. `commit`, `apply`, `last` are reserved names.
- **Expression-bodied functions** (`=>`) whose body is a call on computed arguments can trip E208 ("may return error from call"); the block form with `let` bindings compiles. Not investigated further.
- **Clipboard writes need a behavior edit invocation** (`radiant_dom_edit_invocation`), so an author template cannot put model text on the clipboard. Copy of a selection longer than the window copies only its visible part. Cut is made safe: it deletes exactly the DOM selection it copied.

## 2. Tests

| Test | Covers | Status |
|---|---|---|
| `test/lambda/edit/source_buffer.ls` | round trips (LF, CRLF, mixed, no final newline, UTF-8), chunk index, deltas and inverses, splits, joins across chunks, merges, delete-all | pass |
| `test/ui/edit/edit_src_type_save.json` | open `.txt`, click, End, type, dirty, Cmd+S exact write-back, close | 11/11 |
| `test/ui/edit/edit_src_keys.json` | Enter keeps indentation, Backspace joins, Shift+Alt word selection replaced, Tab, undo/redo (keys and toolbar), typing over a selection undoes as one step | 10/10 |
| `test/ui/edit/edit_src_large.json` | generated 5.7 MB, 100,000-line file: open, PageDown ×2, Cmd+Down to the end, edit, Cmd+Up, edit, exact save | 11/11, about 2.4 s for the whole run |
| `test/ui/edit/edit_src_markdown.json` | `--source` on `.md`: markup shown and kept byte for byte | pass |
| `test/ui/edit/edit_src_ime.json` | preedit updates, commit is one undo step, cancel restores | pass |
| `test/ui/edit/edit_src_scroll.json` | wheel down and up by lines, thumb drag, typing at a caret scrolled out of view | 10/10 |
| `test/ui/lambda_continuous_events.json` | a Lambda page: `mousemove` count and `x`, `wheel` with `deltaY`, element `scroll` (delivered on the next frame), a template without those handlers untouched | 7/7 |
| `test/ui/edit/edit_src_window_select.json` | Shift+PageDown selection past the window is replaced whole; a track click pages the window | pass |
| `test/ui/edit/*.json` through `test_ui_automation_gtest.exe --suite edit` | whole edit suite, including the runner's new `prepare` and `source` keys | 23/25; `edit_md_save_as` and `edit_md_scroll_chrome` fail identically on the unmodified package |
| `make test-radiant-baseline` | the focus-restore change | layout, WPT, render, vector, page suites pass; UI automation 334/335 and the remaining failures (`doc_editor_text_to_pdf`, `CssCascadeMemory` 374,842 vs 373,940 bytes) reproduce identically without the change; the two `dom_jquery_ui_*` failures are the known worktree data-link issue; the view suite failed under load and passes 10/10 alone |

## 3. Remaining P0

- ~~Copy of an off-window selection: needs an author-reachable clipboard write.~~ Done in P3 (§7.1): the model's edit result names the clipboard text.

## 4. OQ14 — resolved by ES23v2 (user, 2026-10-06)

The ES5 hot-path guard kept `wheel`, `scroll` and `mousemove` out of every Lambda author template, so editor-owned scrolling (CED13) had no wheel or thumb drag. The user directed Radiant to deliver these events to Lambda templates. Implemented as ES23v2 (`vibe/Lambda_Design_DOM_Dispatch.md`):

- `event_is_hot_path` now reads the one continuous-event list in `template_registry.cpp`. `mousewheel`, the legacy alias dispatched with every wheel, stays discrete on purpose: adding it broke `doc_editor_html_preview_wheel`, because that discrete event is what first loads the dom package in a static iframe preview so its wheel scrolling works. Lazy package loading riding on a legacy alias is fragile and worth its own fix.
- Both author gates — the shared DOM dispatch (`radiant_author_template_event_live`) and Lambda-only documents (`dispatch_lambda_handler`) — use `author_template_may_handle`: an exact per-event flag for continuous events, the hashed prefilter otherwise. The hashed mask alone would have false positives in a template-heavy document and walk ancestors on every pointer move.
- Package loading and behavior dispatch for continuous events are unchanged.
- Element `scroll` events stay frame-queued (CSSOM View), so a template sees them on the next rendering frame, as in browsers.

## 5. P1 — Markdown highlighting

### 5.1 What was built

| Layer | Owns | Files |
|---|---|---|
| Window parse (CED16v2) | restart scan with the parser's own fence and HTML-block rules, restart state cached per chunk boundary with suspect-entry convergence, a window slice parsed on a private pool released before returning | `lambda/input/markup/markup_highlight.{hpp,cpp}` |
| Span sink | block spans from `parse_document`, link definitions, inline spans from the three inline helpers, a per-call inline origin carried through the three nested re-parse sites, paragraph and heading segments mapping the joined inline text back to lines | `markup_parser.hpp`, `block/block_document.cpp`, `block/block_paragraph.cpp`, `block/block_header.cpp`, `inline/inline_spans.cpp`, `inline/inline_emphasis.cpp`, `inline/inline_link.cpp` |
| Shared parser rules | `is_code_fence_close` extracted from `parse_code_block`; `get_fence_info`, `HtmlBlockType`, `detect_html_block_type`, `check_html_block_end` promoted to `block_common.hpp` | `block/block_code.cpp`, `block/block_html.cpp`, `block/block_common.hpp` |
| `parse()` option | `sourcepos: 'spans'` with `window` and `prescan`; the source may be a string, a line array or the buffer's chunk array | `lambda/runtime/lambda-eval.cpp` |
| Highlighter (CED15) | class map, run painting (outer constructs first), container and fence line marks, link URL parts, front matter, provisional mapping through deltas (CED14v2), restart-cache realignment after chunk rebuilds | `lmd/package/edit/source_highlight.ls` |
| Surface | rows render as classed leaves; caret mapping per leaf; one `settle` step per handler (map, then make the window exact) | `lmd/package/edit/source.ls`, `rich_text.ls` (classed leaf) |

### 5.2 Measurements

| Case | Cost |
|---|---|
| Native window parse, 171 lines at line 50,000 of 100,100, no cache | 1.7 ms |
| Same, with the restart cache | 0.5 ms |
| Whole pass 2 (native parse plus Lambda runs for 171 lines) | about 4 ms |
| 100,009-line Markdown fixture end to end (open, jump to end, type, wheel, jump to top) | 2.7 s |

Fidelity over the 57 top-level corpus files in `lambda-test/markdown`, sliding 40-line windows every 17 lines: 60,255 lines compared, 113 differ (0.19%), in 7 files, and every difference is the same kind: a reference link (`[text][ref]`, `[text]`) whose definition lies outside the parsed window. The full parse resolves it; the window parse does not. The synthetic test `test/lambda/edit/source_highlight_window.ls` (fences and HTML comments across blank lines, front matter, setext headings, tables, lists with indented continuation) has no differences, with or without the cache, and after an edit that opens a fence above the window.

### 5.3 Findings worth keeping

- **Every `parse()` keeps its `Input` until the runtime resets** (all inputs on a thread share one pool): 4,000 parses of a 3 KB document reached 1 GB. A parse per frame would leak, so the window parse runs on a private pool and copies flat spans to the GC heap. The general `parse()` retention is not fixed here; it affects any long-running Lambda application that parses repeatedly.
- **`[lo, hi]` literals are `LMD_TYPE_ARRAY_NUM`**, not `LMD_TYPE_ARRAY`; native code reading an int list must accept both (read through `fn_len`/`fn_index`).
- **There is no frame-request primitive on master.** `dom.request_frame` exists only in uncommitted work in the main checkout. Pass 2 therefore runs before the handler returns (about 4 ms); `settle` in `source.ls` is the one place to change when the primitive lands (OQ16).
- **Containers keep the P1 fallback (OQ10):** inline constructs inside list items and blockquotes are plain; the walker colors their markers from the line text. Five line-array swap sites (`block_quote.cpp` ×2, `block_list.cpp` ×2, `block_paragraph.cpp`) need column maps for P3.

### 5.4 Tests

| Test | Covers | Status |
|---|---|---|
| `test/lambda/edit/source_highlight.ls` | runs for every construct, typing inside a token, newline split, paste, exact re-parse agreeing with provisional runs, cache realignment | pass |
| `test/lambda/edit/source_highlight_window.ls` | window versus full parse, cached versus uncached, cache after an edit | pass, 0 mismatches |
| `test/ui/edit/edit_src_md_highlight.json` | classes for each construct, typing inside bold, typing a new bold span, save | 13/13 |
| `test/ui/edit/edit_src_md_large.json` | 100,009-line Markdown: highlight at both ends, typing, wheel | 9/9 |
| all `test/ui/edit/edit_src_*.json` | P0 behavior with highlighting on | 9/9 |

### 5.5 Open questions raised in P1 — resolved (user, 2026-10-07)

- **OQ15 — the window parse returns flat spans, not a trimmed tree.** Every frame's tree would live in a retained `Input` (see 5.3), so the parse returns `[kinds, spans, states, restart]`. **Decision:** keep flat spans, ruled as CED15v2 and CED16v3. CED18 followed in P2 as CED18v2 (§6).
- **OQ16 — pass 2 on the next frame.** **Decision:** wait for `dom.request_frame`. Pass 2 stays synchronous in `settle` until it lands.

## 6. P2 — HTML highlighting

### 6.1 What was built

| Layer | Owns | Files |
|---|---|---|
| Lexical mode (CED18v2) | `html5_lex_spans`: the tokenizer with the tree builder bypassed; a token's span is the cursor range around `html5_tokenize_next`; attribute name and value spans come from an optional state hook in `html5_switch_tokenizer_state` (null on ordinary parses); the lexer switches to RCDATA, RAWTEXT or PLAINTEXT after the start tags that need it | `lambda/input/html5/html5_parser.h`, `html5_tokenizer.cpp` |
| Window parse | the restart engine is now format-neutral: `RestartRules {step, safe}` per format and one `highlight_window` driver; HTML restart states are comment, CDATA, open tag (with its open quote) and raw-text body (with its end tag); byte offsets in the joined slice map back to lines through the slice's line starts | `lambda/input/markup/markup_highlight.{hpp,cpp}` |
| `parse()` option | `type: 'html'` with `sourcepos: 'spans'` | `lambda/runtime/lambda-eval.cpp` |
| Highlighter | `language_of` maps `.html`/`.htm`; `highlight` takes the language; HTML kinds map to `tok-tag-punct`, `tok-tag`, `tok-attr`, `tok-attr-value`, `tok-comment`, `tok-doctype`, `tok-entity`, `tok-raw`; the outer `tag` span paints first so `<`, `=`, `>` and spaces keep the punctuation class; adjacent runs with one class merge; front matter applies to Markdown only | `lmd/package/edit/source_highlight.ls`, `source.ls` |

### 6.2 Measurements

Debug build, so for orientation only (rule 10): on a 105,000-line HTML buffer, a 171-line window at line 50,000 parses in 1.6 ms cold and 0.08 ms with the restart cache; the whole pass 2 (native parse plus Lambda runs) takes 15 ms.

Fidelity: `test/lambda/edit/source_highlight_html.ls` slides 30-line windows every 7 lines over 600 lines of comments, script and style bodies, and tags whose attributes span lines. Window and full parses agree on every line, with and without the cache, and after an edit that opens a comment above the window.

### 6.3 Findings worth keeping

- **The tokenizer's position is enough for token spans, not for attributes.** A start tag token is produced only at `>`, so attribute ranges must be observed while the tokenizer is in them; the state hook is the smallest seam that reports them, and it costs ordinary parses one null check per state switch.
- **Raw-text switching belongs to the tree builder.** Without it, `<script>` bodies would tokenize as markup. The lexer reproduces only that switch, keyed on the start tag just emitted.
- **The source filename can match a click target.** The fixture's first draft clicked the text "light" and hit the toolbar's `edit_src_html_highlight.html`; target text unique to the document.

### 6.4 Tests

| Test | Covers | Status |
|---|---|---|
| `test/lambda/edit/source_highlight_html.ls` | runs for every HTML kind; window versus full parse, cached and uncached; cache after opening a comment above the window | pass, 0 mismatches |
| `test/ui/edit/edit_src_html_highlight.json` | classes for doctype, tag, attribute, value, comment, entity and raw text in `sample.html`; typing inside an attribute value keeps it colored; save | 10/10 |
| all `test/ui/edit/edit_src_*.json` | P0 and P1 behavior with HTML highlighting added | 10/10 |
| all `test/ui/edit/*.json` | the edit application | 27/29; `edit_md_save_as` and `edit_md_scroll_chrome` fail before this work too |
| `make test-lambda-baseline` | including the HTML5 WPT parser suite (364/364) | 6274/6274 |

## 7. P3 — parity and polish

### 7.1 What was built

| Item | How | Files |
|---|---|---|
| Model clipboard | A model edit result may carry `clipboard_text`. For a copy or cut, Radiant copies the DOM selection before the model handler runs and replaces it with the model's text afterwards (Lambda_Design_DOM_Editable §4.5). The source surface returns its whole model selection, so copy and cut work past the window. Copy did nothing on model surfaces before: the intent went to the model handler, which declined, and nothing copied natively. After P3, the rich Markdown surface still copied nothing: its handler declined the copy, set a "Could not apply copy." status, and re-rendered before the DOM selection was read, and the lost selection then broke a following cut. The copy is now taken first, and the shell claims a copy as a no-op without writing state (`shell.ls`; `test/ui/edit/edit_md_clipboard.json`). | `radiant/event.cpp`, `lmd/package/dom/edit_result.ls`, `source.ls` |
| Paste and the native selection | A key the surface does not handle writes no state: a render would replace the rows the key's native default (paste) targets, and the editaction that follows adopts the DOM selection itself. Every handler that renders without an edit (save, Escape, a no-op Tab or undo) projects the selection again. Paste failed after a click or a save before. | `source.ls` |
| Shift+click | The surface extends the model selection from its own anchor to the press's source position; the native extension starts from the clamped DOM anchor. | `source.ls` |
| Off-window selection | No `tok-selected` runs: the clamped DOM range paints every rendered row between the ends (checked by render). | — |
| Indent, dedent | Tab over lines indents them (empty lines stay empty); Shift+Tab dedents one unit or one tab from the caret's line or each selected line. One delta, one undo step; the selection keeps its text. A dedent with nothing to remove keeps focus. | `source.ls` |
| Find | Cmd/Ctrl+F opens a bar floating over the text's top right, seeded with a one-line selection; it takes focus through `autofocus` after `clear_editing_focus`. Matches ignore case. The open query lives in the paint state, so `row_leaves` lays match runs over the tokens (`syn.overlay`) and selection mapping stays consistent. The count is per chunk, one native `find` over each chunk's joined lines, about 19 ms for 100,000 lines in a debug build. It is recounted after edits while the bar is open. Enter, Shift+Enter, Cmd/Ctrl+G and the buttons step and wrap; typing searches as you type; Escape returns to the selected match. | `source.ls`, `source_highlight.ls` |
| Soft wrap | Design §6.4 "As built": one view record `{handle, top, row, rows, cols, wrap}` replaces `top` and the measured surface; Alt+Z toggles; character wrapping keeps row arithmetic exact; rows scroll inside a line taller than the view. | `source.ls` |
| Column maps (CED17, OQ10) | Containers register their stripped line copies with the span sink (`highlight_push_lines` / `highlight_pop_lines`); a copy line maps to its parent line at the byte offset where it is a suffix, and an unmappable line drops its paragraph's spans. Block and segment positions map through the stack to the window's lines. Inline constructs inside list items, quotes and lazy lines are colored. | `markup_highlight.{hpp,cpp}`, `block/block_quote.cpp`, `block/block_list.cpp` |
| Source↔Rich switch (OQ7) | Design §10: the `edit_doc` root in `edit.ls`, a view button in each toolbar, Cmd/Ctrl+/; the dirty state carries over; a text the rich format cannot keep stays in Source. | `edit.ls`, `shell.ls`, `source.ls`, `toolbar.ls` |

### 7.2 Findings worth keeping

- **Array literals with spreads were typed by the spread operand, not its elements.** `[*[2, 7], *[5, 9]]` was an array of arrays to the type checker, so `{s: c[0]}` chose an array lane and stored null, on both back ends. `resolve_array` (`lambda/runtime/build_ast.cpp`) now takes a spread item's element type from its operand. Regression test: `test/lambda/array_spread_element_type.ls`.
- **One template per result element (fixed after P3).** The render map recorded a single template for each element, so a template whose output was directly another template's output hid the inner template's handlers: its clicks and emitted events reached the outer one. The reverse map now keeps the innermost template and chains the wrappers (`render_map_wrapper_lookup`); emit, author-tier dispatch and the editaction route walk the chain innermost first, and an inner re-render moves every wrapper's result (Lambda_Design_DOM_Dispatch, note to ES27; `test/ui/lambda_template_wraps_template.json`). The edit root renders the surface's `<body>` directly.
- **A re-rendered outer `edit` template resets its nested `edit` templates' state.** Template state is keyed by the model item's identity, and the outer body builds a fresh `<child …>` element each render, with or without a wrapper element. The edit root re-renders only when it swaps surfaces, so the editor is unaffected; a parent that re-renders around a stateful child needs a stable state identity, which is a design question of its own.
- **Unnamed templates get `_interp_view_<n>` with `n` counted per module.** Lookups compare pointers, so this only makes logs ambiguous: every module's first template is `_interp_view_0`.
- **`view` is a reserved word** (it introduces view templates), and a branch that mixes a statement block with a value (`if … { x = 1 } else { emit(…) }`) is rejected (E312).
- **The UI simulator did not know punctuation key names.** `key_combo` with `"key": "/"` sent no key; one-character names now fall back to the typing table (`radiant/event_sim.cpp`).
- **Fixture runner:** a fixture whose document fails to load prints no Result line, so grepping for failures reads as success; `temp/run_src_fixtures.sh` now reports every fixture explicitly.

### 7.3 Tests

| Test | Covers | Status |
|---|---|---|
| `test/ui/edit/edit_src_clipboard.json` | copy in the window; copy and cut of a 93-line selection past the window; paste back exactly; paste right after a click | 14/14 |
| `test/ui/edit/edit_src_shift_click.json` | Shift+click with the anchor above the window; the selection renders over every visible row | 4/4 |
| `test/ui/edit/edit_src_indent.json` | Tab over lines, Shift+Tab twice, undo per step, focus kept on a no-op Shift+Tab | 8/8 |
| `test/ui/edit/edit_src_find.json` | find as you type, count, Enter and Shift+Enter, no results, a far match scrolled into view, Escape and typing over the match | 16/16 |
| `test/ui/edit/edit_src_wrap.json` | wrapped heights for text and gutter, the caret at the end of a 43-row line, the wheel inside it, unwrap | 13/13 |
| `test/ui/edit/edit_src_toggle.json` | Source button, edit in source, Cmd+/ back to rich, dirty kept, save, switch again clean | 15/15 |
| `test/lambda/edit/source_highlight.ls` | inline spans inside list items, continuation lines, quotes, a list in a quote, a lazy line, an item after a tab | pass |
| `test/lambda/array_spread_element_type.ls` | spread element typing, both back ends | pass |
| all `test/ui/edit/edit_src_*.json` | P0–P2 with P3 | 16/16 |
| all `test/ui/edit/*.json` | the edit application under the new root | 33/35; `edit_md_save_as` and `edit_md_scroll_chrome` fail before this work too |

Markdown fidelity over the corpus (window versus full parse, 60,255 lines): 166 lines differ (0.28%), up from 113. Every difference is still a reference link whose definition lies outside the window. There are more of them because links inside list items are now colored in the full parse.

### 7.4 Not done

- Reference links defined outside the window (the remaining fidelity gap); a per-chunk definition-label cache would close it.
- Nested blocks inside containers (a heading in a quote, a fence in a list item) take only the container's marks; their inline spans are colored.
- Caret motion by wrapped rows, word-boundary wrapping, and re-measuring on window resize.
- Pass 2 on the next frame waits for `dom.request_frame` (OQ16).

## 8. P4 — the outstanding items

### 8.1 What was built

| Item | How | Files |
|---|---|---|
| Pass 2 on the next frame (OQ16, CED14v2) | `settle` is pass 1 only (map runs through the edits); `request_exact` asks for a `source_frame` on the surface's `<body>` when the rendered rows are not exact, at most one request outstanding (`frame` holds its token); `on source_frame` swaps the exact runs in with one assignment and projects the selection again; a composing row waits for its commit. Opening a file still highlights the first window at once (line 0, no restart scan). | `source.ls` |
| Frame requests across a rebuild | A reactive rebuild replaces the template's result subtree, so the request owned by the old `<body>` was dropped as detached. `radiant_frame_requests_follow_rebuild` moves pending requests to the structurally corresponding replacement node, beside `view_state_preserve_subtree_identity`; `view_state_nodes_correspond` is promoted to `event.hpp` for it. | `radiant/event.cpp`, `event.hpp`, `state_store.cpp`, `cmd_layout.cpp`; RAD_16 §8 |
| Host config for `lambda edit` pages | The transform loader never received the viewer's host config, so `doc->js.host_ui_context` was null and any re-render outside a native input dispatch (a frame event, the coalesced `selectionchange`) logged "rebuild_lambda_doc_incremental: no document" and was dropped. `view_doc_in_window_with_events_internal` now applies the config to transformed documents too. | `radiant/window.cpp` |
| Dialog fields keep their text | With those re-renders no longer dropped, the shell adopted a selection inside a dialog's text field as an editor selection and re-rendered the dialog, resetting the field (`edit_svg_text`, `edit_svg_resize_style` and one `edit_md_save_as` assertion; the last failed on master already). The shell's `selectionchange` ignores selections inside `input` and `textarea`. | `shell.ls` |
| Reference links across the window (CED16v4) | The parser's link-definition pre-scan is a resumable method (`prescanLinkDefinitions`, `LinkPrescanState`). The highlight cache keeps, per chunk boundary, the restart state and the pre-scan's state (5 ints), and per chunk the labels it defines; the window parse is seeded with every chunk's labels. Markdown boundaries extend to the document's end; a boundary inside a rebuilt range is unknown (kind −1), which also closes a false convergence after a chunk split. | `markup_highlight.{hpp,cpp}`, `markup_parser.{hpp,cpp}`, `lambda-eval.cpp`, `source_highlight.ls` |
| Nested blocks | The quote and list-item loops record their child blocks (`highlight_note_item`) and link definitions through the column maps; the walker paints leaf blocks, then container markers outermost first (a list in a quote colors both), then inlines. A tab after a bullet now counts. | `block_quote.cpp`, `block_list.cpp`, `block_document.cpp`, `source_highlight.ls` |
| Long lines (§5.3) | A line over 10,000 code points gets no runs. | `source_highlight.ls` |
| Status line (§10) | Left: `Ln, Col`, the selection's size (characters in a line, lines across several), the last message. Right: language, `LF`/`CRLF`, `Unsaved`. | `source.ls` |
| Soft wrap motion and resize (§6.4) | Up, Down, PageUp and PageDown step by rows while wrapping, keeping an x within the row; `on resize` measures rows and columns again; `on load` measures at once. | `source.ls` |
| Column units (CED21) | `bytes_before` and `col_at_byte` in one module, each one native UTF-8 encoding instead of per-character prefix sums (the old `col_at_byte` was quadratic in the line length). | `source_units.ls` |
| GTest ring (§11) | (a) span text, (b) window vs full parse by per-line coverage over the corpus, (c) span recording leaves the tree identical, (d) HTML lexer vs tree parser, (e) 300 random edits with the carried cache vs a cold parse. | `test/test_input_sourcepos_gtest.cpp`, input suite |
| Horizontal scrolling (§6.2) | `left` columns in the view; the surface takes a negative `margin-left` and slides under the gutter, which paints above it; `follow` keeps the caret in view with a few columns of context; horizontal wheel deltas scroll by whole columns, carrying the remainder; the measured width subtracts the hidden columns. | `source.ls` |
| Gutter clicks (§6.3) | A press on a line number (`data-line`) selects the line; Shift extends the selection to it. A click past a line's end needed nothing: native hit-testing puts the caret at the end. | `source.ls` |
| Undo groups (§5.2) | A typing step also ends after a 500 ms pause (the events' `time_stamp`) or a caret move (keys, clicks, gutter, Shift+click). | `source.ls` |
| Event `timeStamp` | Native event records built for Lambda handlers had no `timeStamp`, so every handler read 0; they now carry their creation time on the document's clock, as constructed events do. | `radiant/event.cpp` `build_dom_event_record` |
| Caret `selectionchange` | The Lambda `selectionchange` on mouse-up required a non-collapsed range, so a click that only placed the caret reached the model on the next key (the old "adopt it first" comment in `keydown`); the guard now accepts a caret. The surface projects the selection again after adopting it. | `radiant/event.cpp` `dispatch_selectionchange`, `source.ls` |

### 8.2 Measurements

Release build, 100,009-line Markdown stress document (391 chunks), window of 171 lines at line 50,000:

| Case | Cost |
|---|---|
| Native window parse, cold: the boundary and label scan of the whole document | 4.2–4.7 ms |
| Native window parse, with the cache | 0.14 ms |
| Pass 2 with the cache (native parse plus Lambda runs) | 6.4 ms |
| Pass 2 after typing in the window (the edited chunk rescanned, converging) | 6.0 ms |
| Pass 2 after opening a fence at line 1 (every later boundary changes) | 6.1 ms |
| Pass 2 with the P3 walker, same window | 5.5 ms |

The walker's extra cost against P3 comes from the nested-block spans each line is now checked against; it runs on a frame, off the keystroke path. The per-line span filter is O(lines × spans) as before.

Corpus fidelity (GTest (b), `test/markdown`, 40-line windows every 17 lines): 1,577 windows, 60,391 lines, 0 windows differing from the full parse. P3 had 166 differing lines (0.28%), all reference links.

### 8.3 Findings worth keeping

- **`lambda edit` pages had no host UI context.** Every other loader applies `DocumentJsHostConfig`; the transform loader did not, so anything that re-rendered outside a native input dispatch was silently dropped. It also masked the dialog-field bug above.
- **A template cannot own a frame request on its own result without the rebuild rebind.** Slides got away with it because their template's result is static model content, so the rebuild reuses the element.
- **The headless simulator ticks frames only after a JSON event's input turns** (`sim_input_turn_drain`), which every keyboard and pointer event marks; assertions retry across host turns, so fixtures see pass 2's result without `advance_time`.
- **`input(path, 'text')` stops at the first NUL byte** (`input_from_local_path` uses `strlen`); saving such a file from the editor would truncate it. Not fixed: OQ18.
- **Word-boundary wrap cannot be computed in script**: Radiant's `pre-wrap` breaking follows UAX #14 in full. OQ17: the user kept character wrap.
- **The worktree sandbox refuses shell loops and long pipelines**; scripts under `temp/` (`run_units.sh`, small Python patchers) work.

### 8.4 Tests

| Test | Covers | Status |
|---|---|---|
| `test/test_input_sourcepos_gtest.cpp` | design §11 (a)–(e) | 5/5 |
| `test/lambda/edit/source_highlight.ls` | nested blocks (heading in a quote, fence and definition in a list item), a list marker in a quote, a tab after a bullet, the 5-int cache realignment with labels | pass |
| `test/lambda/edit/source_highlight_window.ls` | reference links defined below and above the window, and after an edit adds a definition, with the carried cache | pass, 0 mismatches |
| `test/ui/edit/edit_src_md_nested.json` | nested blocks, references 600 lines below, a 10,800-character plain line, typing keeps the links | 14/14 |
| `test/ui/edit/edit_src_status.json` | selection size, language, line ends, unsaved state | 7/7 |
| `test/ui/edit/edit_src_wrap_rows.json` | Up and Down by rows, resize re-measures (gutter height equals text height) | 10/10 |
| `test/ui/edit/edit_src_hscroll.json` | sideways scroll to the caret, the gutter stays, a click on scrolled text, Home, the sideways wheel, wrap resets it | 11/11 |
| `test/ui/edit/edit_src_gutter.json` | a click past a line's end, a line-number click and Shift+click, typing over the selected line | 4/4 |
| `test/ui/edit/edit_src_undo_groups.json` | undo steps split by a 600 ms wait and by a caret move | 3/3 |
| all `test/ui/edit/edit_src_*.json` | P0–P3 behavior with pass 2 on a frame | 22/22 (`edit_src_wrap` now expects the column scrolled to the caret after unwrapping) |
| all `test/ui/edit/*.json` | the edit application | 42/44; `edit_md_scroll_chrome` (6/7) and `edit_md_view_only_keys` (a CHECK-FAIL crash) fail identically with a base binary built at `796581033`; `edit_md_save_as` now passes |
| `test_ui_automation_gtest --suite baseline` | after the caret `selectionchange` and `timeStamp` changes | 363/365; `doc_editor_text_to_pdf` and `tetris_smoke` fail identically with the base binary |
| `make test-lambda-baseline` | parser, `parse()` | 6282/6286; the four failures fail identically with a base binary: three `LambdaReplSessionTests` expect the `λ>` prompt, which needs a UTF-8 locale the sandboxed shell lacks, and `pdf_svg_page_resources` needs the main checkout's ignored `test/pdf` data |
| `make test-radiant-baseline` | frame rebind, host config, shell | layout, page, vector, page-load, fuzzy, WPT suites pass. Failing, all identical with a base binary built at `796581033`: `doc_editor_text_to_pdf` (1/10), `tetris_smoke` (16/17), CSS cascade memory (jqueryui 378,762 > 373,940, linuxmint 668,986 > 667,901). Failing only for missing worktree data, passing once `test/render` and `test/pdf` are linked: the PDF fixtures, the view suite and `RenderBatchReleasesImageCacheAfterDocumentOwner`; the four `dom_jquery_ui_*` fixtures need the main checkout's `test/jquery-ui` link |

### 8.5 Not done

- Refusing files with NUL bytes (OQ18) is left open by the user; word-boundary wrapping is not built by decision (OQ17).
- Side findings filed as separate tasks: the `edit_md_view_only_keys` crash, a diagnostic for a module-level `let` read before its definition, `parse()` retaining every `Input`, lazy `dom` package loading riding on `mousewheel`, nested template state identity (needs a ruling), and E208 on expression-bodied functions.
