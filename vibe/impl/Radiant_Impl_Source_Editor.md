# Radiant Source Editor — Implementation Plan and Record

**Date:** 2026-10-06
**Status:** P0 committed (`9007c54b3`) and P1, Markdown highlighting, committed (`8c477c3a8`), both on branch `worktree-source-editor`. P2, HTML highlighting, is implemented in the same worktree (§6). OQ15 and OQ16 were decided by the user on 2026-10-07 (§5.5).
**Design:** [Radiant Source Editor](../radiant/Radiant_Design_Source_Editor.md) (CED11–CED22, CED14v2, CED16v2).

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

- Copy of an off-window selection: needs an author-reachable clipboard write (P3 or an engine follow-up).

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
