# Editing viewer document formats

## Current checkpoint

- 2026-11-13; `Lambda-opus`, branch `master`, base `8b5ebe56e` plus
  uncommitted changes for this task. The goal is structured data editing,
  keyboard-oriented rich markup editing, source editing for other text,
  and rejection of non-text media.
- S12.1.1v2 and S12.1.3 govern file effects and editor state. Preserve the
  existing adapter round-trip checks and atomic save/conflict handling.
- CSV and TSV now present a grid with numbered rows and columns under D9.3.4;
  the first row remains editable data. Tab and Shift+Tab commit and move between
  cells, while row and field actions use the existing source-backed model and
  undo history. Focused CSV/TSV edit, navigation, add and delete replays pass,
  as does a separate uneven-row and multiline replay
  (`temp/edit-viewer/csv-grid-focused.log` and `csv-grid-uneven.log`). The
  editor model selection passed 88/88; the edit UI suite passed 140/143 with
  the three previously recorded Markdown failures (`csv-grid-model.log` and
  `csv-grid-suite.log`, under `temp/edit-viewer/`). The suite run predates the
  added uneven-row replay.
- XHTML now has a conservative rich adapter (D10.1.4). It preserves the
  declaration and document envelope and requires the strict XHTML tree to
  survive conversion through the HTML editor before admission. Compact HTML
  serialization and strict XHTML reimport extend coverage to compact documents,
  line breaks, images, lists, tables and preformatted text; other whitespace
  and unsupported roots fall back to source. Widening this coverage requires
  preserving additional XML structure and whitespace. Four XHTML UI replays
  and the extended model check pass (`temp/edit-viewer/xhtml-compact-ui.log`
  and `temp/edit-viewer/xhtml-compact-editor.log`). The editor model selection
  remains 87/88 with the existing LI-044 failure. The edit UI suite passed
  138/141 with the same three Markdown failures
  (`temp/edit-viewer/xhtml-compact-edit-ui.log`).
- Existing Markdown and HTML adapters and rich surface are in
  `lmd/package/editor`; the source surface already supports large files.
  `lmd/package/doc/doc_viewer.ls` lists the viewer's additional markup and
  property formats. Input-owned LI-029–LI-032 block JSON, TOML, YAML,
  iCalendar and vCard property editing: their parsers accept malformed input.
  The attempted compensations were removed under the cross-project rule;
  JSON, YAML and TOML retain their pre-existing source surfaces; iCalendar
  and vCard now also open as source. Java
  properties, INI and strict XML values use the property tree; CSV/TSV use
  the shared surface's grid presentation.
  CSV/TSV fields display decoded values, quote edits automatically, and support
  multiline editing; unchanged fields retain their original spelling and line endings.
  Input-owned LI-034 blocks property editing when the CSV delimiter detector
  counts tabs inside a quoted first record; those files fall back to source.
  Each uses source spans to retain untouched text; XML attributes can be added,
  renamed, removed and edited. XML values decode entities, escape edits, and
  support multiline text, CDATA and empty elements. Child elements can be
  added, renamed or removed, including inside a self-closing parent; the root
  can be renamed but not removed, and untouched comments and markup are retained.
  XML edits reparse
  the whole candidate as strict XML and reject injected or duplicate names
  and invalid CDATA. XML and CSV
  values use stable text controls so saving across single-line/multiline
  changes retains focus and the caret.
- Property edits and insertions now verify that the INI/properties source
  parser retained the entered value exactly. Unquoted INI comment delimiters
  and edge whitespace that would be discarded leave an invalid draft instead
  of silently truncating data; quoted INI values remain editable. The change
  also checks both ends of the replaced span. The regression reproduced
  before the fix; 85 focused model tests and three INI/properties UI replays
  pass (`temp/edit-viewer/key-value-focused.log` and `key-value-ui.log`).
  Property-name renames likewise require an exact current span and reparse the
  candidate to check both name and value; duplicate-name and Unicode tests and
  a rename/undo/save UI replay pass. Input-owned LI-045 separately blocks
  correct Java properties with escaped key separators or spaces: its parser
  and the editor's existing scanner both split those names incorrectly. After
  the Input fix, update the scanner and test escaped keys without compensating
  for the provider defect. The focused model run passed 89/90 with LI-044
  failing, and the edit UI suite passed 121/124 with the same three known
  failures (`temp/edit-viewer/property-rename-focused.log` and
  `property-rename-edit-ui.log`).
  Adding INI/properties entries and CSV/TSV rows now uses the last existing
  line-ending style (LF by default), including files without a final newline.
  CRLF, LF and mixed-ending model tests and a CRLF UI save replay pass. The
  editor model selection is 87/88 with LI-044 still failing; the full edit UI
  suite is 114/117 with the three previously reproduced Markdown failures
  (`temp/edit-viewer/property-lines-focused.log` and `property-lines-edit-ui.log`).
  Deleting a row's only CSV/TSV field now removes that row; the format has no
  zero-field row representation. Tests cover blank, quoted, multiline, CRLF
  and unterminated rows, plus UI undo/redo and save. The focused selection
  passed 90/91 and the edit UI suite passed 134/137, with the same known
  Markdown failures (`temp/edit-viewer/csv-delete-single-focused.log` and
  `csv-delete-single-edit-ui.log`).
  An invalid draft also remains bound to its original field: save and source
  shortcuts from another field cannot clear it, and input there returns focus
  to the draft. The new UI replay reproduced the lost draft before the fix.
  Afterward, 85 focused tests passed and the edit UI suite passed 108/111,
  with the same three previously reproduced Markdown failures
  (`temp/edit-viewer/property-draft-focused.log` and `property-draft-ui.log`).
  Enter commits a single-line property field to document history; Escape
  discards its pending or invalid draft while retaining other dirty changes.
  Multiline controls retain Enter for newlines. The shared commit path handles
  change, save and source-switch operations as well. UI replays cover valid,
  invalid, undo/redo, dirty-window and multiline cases. The focused model run
  passed 89/90 and the edit UI suite passed 128/131, with the same known
  Markdown failures (`temp/edit-viewer/property-keyboard-focused.log` and
  `property-keyboard-ui.log`).
  Add-property, array-row and XML-child dialogs focus their first field and
  submit on Enter using the same action as their Add button. Invalid entries
  remain editable; successful insertion returns focus for keyboard undo.
  Four targeted UI replays passed, and the edit suite passed 132/135 with the
  same three known Markdown failures. The focused model selection remained
  89/90 with LI-044 (`temp/edit-viewer/property-add-key-ui.log` and
  `property-add-key-focused.log`).
- The shared lightweight-markup adapter admits RST, Org, Wiki, AsciiDoc and
  Textile only when parsing and writing preserve the document; unsupported
  documents open as source. RST rich edit/save passed. Rich HTML tables now
  expose a context menu for row and column changes. Shift+F10 opens it from a
  cell; Tab and Enter choose actions, and Escape restores the document caret.
  Adding a row or column moves the caret to its new cell; deleting one moves
  it to a surviving cell, including when the rows have different widths.
  Mouse and keyboard edit/save replays cover both paths.
  Input-owned LI-033 blocks assigning exact authored spans to Markdown
  footnote definitions; their generated footer has no per-definition source
  positions. Two existing view-only UI failures reproduce this limitation.
- Cmd/Ctrl+K opens the link dialog from a rich-text selection; the address
  field takes focus, Enter inserts, and Escape returns to the selection.
  Format capability and view-only guards remain shared with the toolbar.
  New links with no title omit the attribute: an empty title was lost by the
  Markdown writer, causing the existing round-trip guard to refuse save.
  Model round-trip and keyboard UI replays pass, including RTF refusal. The
  edit suite passed 110/113 with the same three known Markdown failures, and
  86 focused tests passed (`temp/edit-viewer/link-key-ui.log` and
  `temp/edit-viewer/link-key-focused.log`); the Escape replay was run afterward.
  Cmd/Ctrl+Alt+1–3 and +0 apply heading levels 1–3 and paragraph style through
  the same format and view-only guards as the toolbar. Markdown and HTML
  edit/save replays cover both modifier styles and HTML undo/redo; RTF refuses
  the unsupported command. The edit UI suite passed 113/116, with the same
  three Markdown failures, and 86 focused tests passed
  (`temp/edit-viewer/heading-key-ui.log` and `heading-key-focused.log`).
  Cmd/Ctrl+E toggles inline code and Cmd/Ctrl+Shift+X toggles strikethrough;
  both use the toolbar's capability and view-only checks. Replays cover
  stored marks, selected text, RTF support and refusal, and view-only math.
  The edit UI suite passed 125/128 with the same three known failures, and the
  focused model selection passed 89/90 with LI-044 still failing
  (`temp/edit-viewer/inline-keys-ui.log` and `inline-keys-focused.log`).
  Cmd/Ctrl+Shift+7 and +8 create numbered and bulleted lists through the same
  command and format guards. Markdown and HTML replays cover save and undo;
  RTF declines list commands. The edit UI suite passed 116/119 with the same
  three previously reproduced failures; the focused selection passed 87/88,
  with LI-044 still preventing link-label round trip
  (`temp/edit-viewer/list-keys-ui.log` and `list-keys-focused.log`).
  Repeating a list command now unwraps the selected list items; choosing the
  other list kind changes those items instead of wrapping the whole list in a
  new item. Before/after fragments retain ordered-list numbering. Mixed inline
  content and nested blocks retain their order; item or list attributes that
  cannot safely survive unwrapping or splitting cause the command to decline.
  Model tests cover selected ranges, nested blocks, numbering and attribute
  guards; Markdown and HTML UI replays cover toggle, save and undo. Focused
  checks passed 88/89 with the LI-044 failure, and the edit UI suite passed
  118/121 with the same three prior failures (`temp/edit-viewer/list-toggle-*`).
  List creation, unwrapping and list-kind changes now map text selections to
  their new paths, retaining offsets and selection direction. Model tests
  cover multiple items and nested blocks; UI replays type immediately after
  creation and after unwrapping a middle item. The focused selection remains
  88/89 and the edit UI suite is 120/123 with the same known failures
  (`temp/edit-viewer/list-caret-focused.log` and `list-caret-ui.log`).
  A half-open selection ending at the next item's start now excludes that
  item, while preserving both selection endpoints through list splits. Tests
  cover forward and reversed selections, conversion and unwrapping, retained
  numbering, and an endpoint inside a later inline run. The focused selection
  passed 89/90 and the edit UI suite 125/128, with the same known failures
  (`temp/edit-viewer/list-boundary-focused.log` and `list-boundary-ui.log`).
  Ordered-list fragments also account for preceding explicit `li value`
  resets, and remove a stale `start` when the fragment resumes at 1. The
  explicit-value and negative-start model cases and an HTML save/undo replay
  pass. The focused selection remains 89/90 and the edit UI suite is 133/136,
  with the same known failures (`temp/edit-viewer/list-ordinal-focused.log`
  and `list-ordinal-ui.log`).
  A full baseline exposed a regression in Word's list-style command: list
  conversion rejected its `style` attribute. Conversion now removes the old
  marker style while preserving unrelated CSS; unwrapping still declines when
  it would discard other list styling. Word's 35 focused tests and the HTML
  style-switch replay pass. The rerun Lambda baseline is 7,499/7,500, with
  only LI-044 failing (`temp/edit-viewer/word-list-style-lambda-baseline.log`).
  The Radiant baseline is 8,166 passed, 1 failed, 297 partial and 20 skipped;
  `Scene3dPlatform.NativeHostCleanupRetainsTheApplicationRuntime` failed in
  three isolated repetitions because GLFW returned no monitors. A separate
  GLFW probe returns no monitors immediately after initialization, before any
  host cleanup, while CoreGraphics reports one online display; the exact
  environment or platform cause remains unconfirmed. The edit UI suite is
  136/139 with the same three earlier Markdown failures, and the focused
  editor/RTF selection is 94/95 with LI-044. Evidence uses the
  `temp/edit-viewer/word-list-style-*` and `scene3d-cleanup-unsandboxed.log` paths.
- Markdown's round-trip normal form previously trimmed every text fragment,
  treating `one [link](url) two` as equal to `one[link](url)two` and potentially
  accepting visible whitespace loss. It now trims at line and block boundaries
  while retaining spaces at inline boundaries and inside links. The new
  distinction test passes (`temp/edit-viewer/md-whitespace-regression.log`).
  The stronger check exposes Input-owned LI-044: the Markdown parser drops a
  link label's trailing space. `editor_markdown_link_roundtrip` now fails for
  that case rather than accepting a lossy save; do not weaken its expected
  result. The editor UI suite remains 113/116, with the same three previously
  reproduced failures (`temp/edit-viewer/md-whitespace-ui.log`); the 82-test
  model selection is 81/82 until LI-044 is resolved
  (`temp/edit-viewer/md-whitespace-focused.log`). The math-scroll failure has
  an 8.8 px rendered formula width versus the fixture's 5.5 px expectation,
  plus a 2.4 px vertical discrepancy (`temp/edit-viewer/math-investigate.log`).
- Manual pages remain source-only. A trial using the shared markup adapter
  found that the input parser displays the zero-width `\\&` escape as `&`
  (input-owned LI-035). The Lambda `lambda.doc.misc` man writer previously
  emitted section headings on the line after `.SH`, producing empty headings.
  It now emits a quoted, single-line argument for `.SH` and `.SS`, escapes
  embedded quotes, and rejects control characters; the focused writer and
  existing misc round-trip tests pass (`temp/edit-viewer/man-heading-after.log`).
  A broader editor and session selection passed 84/84
  (`temp/edit-viewer/man-heading-editor-check.log`).
  The reproduction is `temp/edit-viewer/man-roundtrip.ls` and its log. Rerun
  editor round-trip and edit/save cases after the Input parser issue is fixed;
  do not admit a lossy adapter.
- Basic RTF paragraphs, emphasis and ordinary font changes have a rich
  adapter with a bounded writer and model round-trip check (D9.3.4). Ordinary
  ANSI font tables and font selection are retained in the source envelope and
  shown on the editing surface. Per-run font and size changes retain their font
  IDs and render through vetted span styles; missing or duplicate font IDs are
  rejected. Non-ANSI font charsets, embedded
  fonts, unknown controls and destinations open as source, preserving metadata the reader
  would otherwise skip (`doc/Markup_Formats_Support.md` §3.3).
  The reader dropped supplementary characters because `u` passed each UTF-16
  surrogate separately to `chr`. It now joins valid pairs, retaining the old
  unpaired-unit behavior (S2.5.8). Direct and grammar-reader tests and the
  existing RTF oracle suite pass; see `test/lambda/rtf_unicode_surrogates.ls`.
  Windows-1252 hex escapes and named punctuation are admitted. The reader and
  PDF package share the same Windows ANSI mapping; other explicit code pages
  remain source-only. `edit_rtf_escaped` and `edit_rtf_windows` verify that
  characters survive a rich edit and save.
  The reader's optional `preserve_empty` mode retains explicit blank paragraphs
  without changing its default output; the editor uses it and writes trailing
  blank paragraphs with their own terminators. Direct, grammar, and UI replays
  cover leading, interior, and trailing empty blocks. An empty document gets
  an editable placeholder paragraph while its original bytes remain intact.
  Document-level `viewkind`, `viewscale` and `viewzk` preferences are retained
  across rich saves. Nested or body-level view controls still open as source;
  `plain` resets character formatting, including font and size, even after a
  document-wide font override. Model and edit/save replays cover both cases.
  Ordinary RGB color tables and per-run foreground colors map to the shared
  span-color marks; rich save retains the table and reconstructs color runs.
  Unsupported color metadata, invalid indices and colors outside the table
  remain source-only or are rejected on save. Model and UI tests cover
  mixed colors, emphasis, preservation, and invalid tables.
  Superscript and subscript runs use the existing rich-text marks and survive
  editing and saving; parameterized baseline controls stay source-only.
  The RTF writer derives its base font from the retained header, rather than
  the last body run, so an ungrouped final font change remains local to that
  run. Model tests cover grouped and ungrouped changes combined with color and
  superscript; `edit_rtf_font_runs` edits and saves a styled run.
  Header `info` and starred `generator` groups are now retained byte-for-byte
  across rich saves, with their original Unicode fallback count restored around
  each group. Binary, incomplete and body-level metadata remain source-only.
  Model and UI edit/save tests pass; the focused selection passed 94/95 and
  the edit UI suite 135/138, with the same known Markdown failures
  (`temp/edit-viewer/rtf-info-focused.log` and `rtf-info-ui.log`).
  Document-level `deflang`, `deflangfe` and `adeflang` are also retained;
  nested, late, invalid and per-run language controls stay source-only.
  Microsoft describes `deflang` as a document language default in its
  [text object model reference](https://learn.microsoft.com/en-us/windows/win32/controls/about-text-object-model).
  A model and rich edit/save replay pass. The focused selection is 95/96 and
  the edit UI suite is 137/140, with the same known Markdown failures
  (`temp/edit-viewer/rtf-language-focused.log` and `rtf-language-ui.log`).
  Paragraph alignment is displayed and preserved on save, including inherited
  alignment and `pard` resets. The current reader captures paragraph formatting
  at its first text run; a formatting control later in that paragraph opens in
  source mode. The retained native-oracle `reset` case depends on this reader
  behavior, so correct that compatibility contract before admitting late
  controls. Cmd/Ctrl+Shift+L/E/R/J changes selected paragraph alignment with
  undo and save; the keyboard replay and multi-paragraph model tests cover it.
  The MediaWiki writer no longer wraps a literal period in `nowiki`, allowing
  ordinary headings and emphasized prose ending in a period to use rich edit.
  Plain-text MediaWiki `nowiki` spans are compared as lexical guards rather
  than semantic markup, and attribute-free parser spans are transparent, so
  literal wiki syntax and links can be edited, saved and reopened. Other
  elements and attributes remain in the preservation check;
  unsupported templates still fall back to source.
  MediaWiki lists, links, basic tables and standalone preformatted blocks
  have model round-trip coverage. A parser-only paragraph around a standalone
  pre block is treated as transparent; a mixed paragraph or attributed pre
  still falls back to source. UI replays cover editing and saving a pre block
  and adding a table row; a model test reopens the changed table.
  Input-owned LI-038 and LI-039 block reliable rich round trips for common
  AsciiDoc headings and Textile spans, respectively; the native AsciiDoc
  heading mapping disagrees with its writer and Textile changes visible text.
  Input-owned LI-040–LI-043 cover Org tables and source blocks, RST literal
  blocks, Org paragraphs following lists, and Org slash emphasis. Focused UI
  replays confirm source fallback for the affected structures and rich edit
  and save for an Org document with a heading, link and a final list.
  Positive left/right indents and line spacing are rendered and retained,
  including inherited settings and paragraph resets. Negative and late
  measurements open in source mode; the editor does not yet expose commands
  for changing these measurements.
- `.txtl` uses the Textile rich adapter and `.ical`, `.1m` and `.3p` open in
  source mode. The Textile edit/save replay and XML attribute operations pass.
- SVG, graph, LaTeX, slides and diff suffixes now select the source surface;
  known binary media and containers are rejected even with `--source`.
- Unlisted and extensionless filenames also open as source after the same
  byte-for-byte, UTF-8 and NUL checks. Save As permits their original suffix
  (including no suffix), and known image suffixes remain blocked. Procedural
  and UI tests cover normal save, Save As, invalid UTF-8 and ASCII PPM refusal.
  The source-routing focused run passed 188/188 tests; the full edit UI run
  passed 106/109, with the same three previously reproduced Markdown failures
  (`temp/edit-viewer/source-unknown-focused.log` and
  `temp/edit-viewer/source-unknown-edit-ui.log`).
  The Lambda baseline passed 7,491/7,491 after this change
  (`temp/edit-viewer/source-unknown-lambda-baseline.log`).
  The Radiant baseline passed with 8,167 passing, 297 partial and 20 skipped
  (`temp/edit-viewer/source-unknown-radiant-baseline.log`).
- Save, conflict and close handling reuse the editor file-session module.
- Known binary media are rejected even with `--source`; a PDF header check
  also rejects ASCII PDFs using a text suffix. GIF signatures and Netpbm
  headers with numeric dimensions in the first 4,096 characters are checked
  too, including ASCII images without an image suffix. A byte comparison, strict
  UTF-8 validation and embedded-NUL check prevent unsafe text reads from
  being written back. Legacy-encoded files require conversion first.
  The content check reproduced acceptance of renamed ASCII PPM and GIF before
  the change; 83 focused tests and three source UI replays pass afterward
  (`temp/edit-viewer/media-content-focused.log` and `media-content-ui.log`).
- The compact rich toolbar groups format, list and insert actions, and keeps
  Select All intact when a menu opens. RTF strikethrough is available while
  unsupported inline code remains blocked. Invalid property drafts stay
  visible and block save; closing offers discard.
- Property-tree disclosure buttons collapse nested entries without changing
  the file. They expose `aria-expanded` and respond to Enter and Space. Stable
  path-based IDs allow the host to restore focus when the tree is rebuilt;
  `test/ui/edit/edit_property_tree_collapse.json` verifies consecutive Enter
  and Space activations without refocusing. The event simulator's `focus`
  action clicks the target, so the earlier test did not exercise keyboard
  activation as intended.
- After removal of the Input parser compensations, 182 focused editor, RTF, PDF,
  UTF-8 and session tests passed. The current editor UI suite passed 83/86; basic rich RTF
  editing and complex RTF source fallback both passed.
  Five SVG drawing replays now use a dedicated package host and passed 71/71
  assertions. The same executable with the
  five original package files restored reproduced the other three failures:
  `edit_md_math_scroll`, `edit_md_view_only`, and `edit_md_view_only_keys`.
  The note application suite also passed 16/16 after the shared surface change.
  The child-element UI replay covers expansion, removal, undo and save; the
  same replay renames both a child and the root before saving. Model checks
  cover paired and self-closing tag renames and reject root removal, malformed
  names and unbound prefixes. See `temp/edit-viewer/xml-rename-all-ui.log` and
  `temp/edit-viewer/xml-elements-edit-ui.log` and `base-*.log`. `make build` was denied writing the sibling build log by
  the sandbox; a Lambda-only link against existing debug sibling archives
  produced `temp/edit-viewer/lambda-tested` (SHA-256
  `7ea4153eff55d41ddb1e03f5506576962aeab781be3259b5e418faffc3be4bf5`),
  and its six focused UI replays passed. The latest authorized standard
  Lambda baseline passed 7,481/7,482; `proc_sync_rtc` reproduced with WebRTC
  unavailable. Test262 previously
  passed 40,261/40,261 selected tests with zero regressions (2,652 skipped).
  The Lambda baseline after the XML value and stable-control changes passed
  7,481/7,482; `proc_sync_rtc` still requires unavailable WebRTC. An intermediate
  run was interrupted to fix a reproduced focus regression; its corrected rerun
  is at `temp/edit-viewer/xml-stable-lambda-baseline.log`. The Radiant baseline
  passed with 8,167 passed, 297 partial and 20 skipped. The child-element
  milestone repeated the Lambda baseline with the same 7,481/7,482 result and
  the Radiant baseline with the same counts; see `temp/edit-viewer/xml-elements-lambda-baseline.log`
  and `temp/edit-viewer/xml-elements-radiant-baseline.log`.
  After element renaming, 74 editor model tests and all 12 XML UI replays
  passed; the aggregate baselines above predate the rename change.
  After focus restoration, the 74 editor tests passed and the edit UI suite
  passed 84/87, with the same three Markdown failures; the aggregate
  baselines also predate this change (`temp/edit-viewer/tree-focus-edit-ui.log`).
  After retaining RTF view preferences, the 182 focused tests passed and the
  editor UI suite passed 85/88 with the same three Markdown failures
  (`temp/edit-viewer/rtf-view-header-focused.log` and
  `temp/edit-viewer/rtf-view-header-edit-ui.log`).
  After keyboard table actions, 74 editor tests passed and the edit UI suite
  passed 87/90 with the same three Markdown failures. The Lambda baseline
  passed 7,481/7,482 with the same unavailable WebRTC test, and the Radiant
  baseline passed with 8,167 passed, 297 partial and 20 skipped. Logs use the
  `temp/edit-viewer/table-keyboard-` prefix.
  After deletion caret handling, 75 editor tests passed and the edit UI suite
  passed 88/91 with the same three Markdown failures. The prior aggregate
  baselines predate this last change; see `temp/edit-viewer/table-delete-*`.
  After RTF color support, 183 focused tests passed and the edit UI suite
  passed 89/92, again with the same three Markdown failures. See
  `temp/edit-viewer/rtf-color-focused.log` and `rtf-color-edit-ui.log`; the
  Lambda baseline passed 7,482/7,483 with the same unavailable WebRTC test;
  see `temp/edit-viewer/rtf-color-lambda-baseline.log`. The latest Radiant
  baseline predates the RTF color and table-deletion changes.
  After RTF superscript and subscript support, 183 focused tests passed and
  the editor UI suite passed 90/93 with the same three Markdown failures; the
  aggregate baselines predate this change. See `temp/edit-viewer/rtf-script-*`.
  After per-run RTF font support, 183 focused tests passed and the editor UI
  suite passed 91/94 with the same three Markdown failures. The Lambda
  baseline passed 7,486/7,486; the previously failing WebRTC test did not
  fail in this run. The Radiant baseline passed with 8,167 passed, 297 partial
  and 20 skipped. See `temp/edit-viewer/rtf-font-run-*`.
  After RTF `plain` resets, 183 focused tests passed and the editor UI suite
  passed 92/95 with the same three Markdown failures. The aggregate baselines
  predate this reset change; see `temp/edit-viewer/rtf-plain-*`.
  After RTF alignment preservation, 184 focused tests passed and the editor UI
  suite passed 93/96 with the same three Markdown failures. The aggregate
  baselines predate this change; see `temp/edit-viewer/rtf-align-*`.
  After alignment shortcuts and selection-boundary repair, 184 focused tests
  passed, the editor UI suite passed 94/97 with the same three Markdown
  failures, and the Lambda baseline passed 7,487/7,487. The Radiant baseline
  passed with 8,167 passed, 297 partial and 20 skipped. Evidence is under
  `temp/edit-viewer/rtf-align-command-*`.
  After RTF paragraph measurement preservation, 184 focused tests passed and
  the editor UI suite passed 95/98 with the same three Markdown failures; the
  aggregate baselines predate this change. See `temp/edit-viewer/rtf-indent-*`.
  After the MediaWiki writer change, 78 focused editor and wiki tests passed;
  the editor UI suite passed 96/99 with the same three Markdown failures.
  The Lambda baseline passed 7,488/7,488; the Radiant baseline passed with
  8,167 passed, 297 partial and 20 skipped. See `temp/edit-viewer/wiki-period-*`.
  After MediaWiki literal-span comparison, 79 focused editor and wiki tests
  passed and the editor UI suite passed 97/100 with the same three Markdown
  failures. The aggregate baselines predate this change; save/reopen and
  unsupported-template cases are in `temp/edit-viewer/wiki-literal-*`.
  After transparent parser-span comparison, 79 focused tests passed and the
  editor UI suite passed 98/101 with the same three Markdown failures. See
  `temp/edit-viewer/wiki-spans-*`; aggregate baselines predate this change.
  After MediaWiki pre-block and table checks, 80 focused tests passed and the
  editor UI suite passed 100/103 with the same three Markdown failures. See
  `temp/edit-viewer/wiki-block-*`; aggregate baselines predate this change.
  The Org/RST coverage pass retained 80 focused tests and 104/107 editor UI
  replays, with the same three Markdown failures. See
  `temp/edit-viewer/markup-coverage-*`; no engine code changed in that pass.
  Calendar/contact source and RTF rich/fallback UI replays all pass.
- Next: after Input fixes LI-029–LI-032, LI-034 and LI-045, implement property editing for the
  affected data formats without compensating for parser defects. After LI-033,
  use the provided source ranges for individually retained footnote blocks.
  Independently expand the rich RTF subset only with preservation evidence,
  and implement remaining markup adapters after LI-035 and LI-038–LI-044.
  Do not silently rewrite source or
  change sibling providers.
