# Word

A native Lambda/Radiant word-editor demo ported from the core workflow in
[`ref/wordcraft`](../../../ref/wordcraft). The application, document model,
commands, ribbon, and dialogs are Lambda; Radiant supplies layout, painting,
input, clipboard transport, and selection geometry. No Rust binary, WASM,
JavaScript editor, or browser is required.

Run from the repository root:

```sh
make build
./lambda.exe demo word
# Or open the entry script directly:
./lambda.exe view test/demo/word/word.ls
```

The studio handbook is editable. Start a blank document with **New**, or return
to the example with **Studio example**. Select text to format it; formatting
at a caret sets the marks for subsequent typing.

## Available workflow

- Home ribbon: undo/redo; bold, italic, underline, strike, subscript and
  superscript; font family and size; text color and highlighting; paragraph
  alignment and line spacing; six paragraph styles; bullets, numbering, and
  list indent/outdent.
- Insert ribbon: a 3 × 3 table, row/column insertion and deletion, links, and
  horizontal rules. Table cells contain editable text.
- Navigation: document headings, literal case-sensitive search across
  formatting runs within a paragraph, previous/next match, replacement, and
  one-step undo for Replace all.
- View: navigation pane, read mode, focus mode, and 50–150% zoom. The editor
  uses a continuous A4-width canvas; PDF export paginates on A4 paper.
- Native typing, paragraph splitting/joining, selection, deletion, composition,
  and clipboard actions use the existing `lambda.editor` protocol.
- Open editable `.word.json` snapshots, HTML, Markdown, text, and supported
  semantic DOCX content. Imports become copies. Unsupported model nodes are
  rejected rather than discarded.
- Save lossless version-1 `.word.json` snapshots with atomic writes, round-trip
  validation, an existing-file prompt, and external-change detection. Unsaved
  changes guard New, Open, sample replacement, and window close. Export HTML,
  text, or paginated PDF; exports leave the editable checkpoint unchanged.

| Shortcut | Action |
| --- | --- |
| Cmd/Ctrl + B / I / U | Bold / italic / underline |
| Cmd/Ctrl + Z | Undo |
| Cmd/Ctrl + Shift + Z, Cmd/Ctrl + Y | Redo |
| Cmd/Ctrl + S / Shift + S | Save / Save as |
| Cmd/Ctrl + F | Find |
| Escape | Cancel a dialog or leave focus mode |

Save and export default to `temp/word/`. Other destinations must have an
existing parent directory. `.word.json` is the editable format; DOCX writing,
RTF/ODT, spell/grammar checking, tracked changes, comments, collaboration,
mail merge, equations, section/header/footer editing, and Word-compatible
pagination are outside this demo. Font substitution follows the installed
fonts available to Radiant. Search does not span paragraph boundaries.
PDF text is currently painted as glyph outlines rather than selectable text.
DOCX bookmarks and retained raw HTML are rejected as unsupported nodes; source
documents remain untouched. Imported image paths should be absolute or data URLs.

## Source analysis and port decisions

WordCraft is a layered word processor, not only an egui text widget. Its
`crates/doc` model owns paragraphs, character runs, tables, sections, and
secondary stories. `crates/engine/src/session.rs` owns the document, selection,
history, layout cache, and view state. `Session::run` resolves a `CommandSpec`,
checks availability, checkpoints mutations, executes the command, and restores
the previous document on failure. The ribbon, shortcuts, CLI, and MCP share
those command IDs. `crates/layout` performs shaping and pagination; the egui
front end paints that derived output. File adapters belong to separate crates.

This port keeps the command/session boundary and translates document operations
into Lambda's existing immutable editor transactions. It reuses the editor's
insertion, deletion, structural editing, history, IME, and DOM-selection bridge
instead of introducing another editing engine. **S12.1.3** makes the template
body a pure projection and handlers the state owner; **D7.2.5** keeps editing
policy in Lambda packages, with Radiant providing the native mechanisms.

| WordCraft source | Lambda implementation |
| --- | --- |
| `crates/doc/src/lib.rs`, `para.rs`, `props.rs` | `lambda.editor.mod_doc` nodes, text leaves, and marks; `mod_model.ls` adds selected-range CSS formatting |
| `crates/engine/src/lib.rs` (`CommandSpec`, `Registry`) | `mod_model.ls` command registry, retaining corresponding upstream IDs |
| `crates/engine/src/session.rs` (`Session::run`, history) | `mod_app.ls` controller plus `lambda.editor` transaction/history facade |
| `crates/engine/src/cmd/{format,para,text,table,edit}.rs` | Existing editor commands plus range formatting, paragraph attributes, style application, and cross-run replacement |
| `crates/engine/src/cmd/find_pattern.rs` | Literal paragraph search mapped back to source positions; wildcard/regex language is not ported |
| `crates/ui-egui/src/{ribbon,canvas}.rs` | `mod_ui.ls`, `mod_surface.ls`, `word.css`; native HTML/CSS layout and caret geometry |
| `crates/engine/src/sample.rs` | Adapted studio handbook in `mod_document.ls` |
| `crates/{docx,formats,pdf}` and `engine/src/io.rs` | Existing Lambda format adapters, conflict-aware `lambda.edit.session`, and Radiant PDF export through `mod_io.ls` |

Character formatting splits only the selected portions of boundary leaves.
Their selection positions are remapped explicitly, including backwards ranges;
ordinary replace-step mapping would collapse positions inside a replaced leaf.
Replace all applies existing insertion transactions from the final match
backwards and composes their steps into one history item. View changes and
selection movement never change the saved document checkpoint (**S1.4**,
**D7.2.1**).

Two integration details matter when extending the demo: `file` is a reserved
absolute-path scheme (**S2.4.5v2**), so file-session state uses `file_state`;
and `emit()` walks to ancestor templates, so root keyboard handlers call the
same controller directly. Boolean HTML attributes are omitted when false.

The demo also exposed a cached-import defect: cloning two cached import cones
could instantiate their shared dependency twice in one runtime. Registering
the second shell rebound the logical code unit to its uninitialized slab while
JIT-promoted functions were executing the first import. The loader now reuses
the existing non-retired instance, preserving execution-local state ownership
(**D7.2.1**, **D8.5.1v7**). The native regression is
`InputScriptCacheTest.CachedDiamondKeepsSharedPromotionSlab`.

Native selection restoration chooses the most specific recorded source owner
before an ancestor's structural fallback (**D7.2.5**). Generated table wrappers
change DOM child indices; accepting the document-root guess first moved clicks
to other cells or paragraphs. The pointer regression clicks empty and populated
cells, types without resetting selection, and repeats after zooming.

Caret and selection geometry use viewport coordinates throughout (**D7.2.5**).
The pointer's block origin already includes scrolling; saving that scroll again
as an iframe translation shifted the caret above the clicked line. The scroll
regression verifies the caret's painted pixels as well as focus and typing.

## Verification

```sh
python3 test/demo/word/run_tests.py
python3 test/demo/word/run_tests.py --model-only
./test/test_input_script_cache_gtest.exe --gtest_filter='InputScriptCacheTest.CachedDiamondKeepsSharedPromotionSlab'
./test/test_source_pos_bridge_gtest.exe --gtest_filter='SourcePosBridgeRoundTrip.*'
./test/test_state_store_gtest.exe --gtest_filter='StateStoreDomMutationTest.CaretFrameOffsetExcludesAncestorScroll'
```

The runner checks model, snapshot, and HTML/Markdown/text/DOCX import goldens on
interpreter, auto, and JIT tiers; then replays typing/history/save,
formatting/search/table, open/export/dirty-guard/focus, table-caret, and scrolled-caret workflows
in headless Radiant. It checks the result records and exported file contents,
and writes its report and logs
under `temp/word/tests/`. The initial and edited screenshots are
`temp/word-initial.png` and `temp/word-edited.png`. Test reruns replace only
their own named files under `temp/word/`.

WordCraft's original sample text and command vocabulary are used under its
MIT license, included as [LICENSE-MIT](LICENSE-MIT). The workspace styling is
original; no Microsoft assets or fonts are bundled.
