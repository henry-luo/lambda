# Editing viewer document formats

## Current checkpoint

- 2026-11-13; `Lambda-opus`, branch `master`, base `8b5ebe56e` plus
  uncommitted changes for this task. The goal is structured data editing,
  keyboard-oriented rich markup editing, source editing for other text,
  and rejection of non-text media.
- S12.1.1v2 and S12.1.3 govern file effects and editor state. Preserve the
  existing adapter round-trip checks and atomic save/conflict handling.
- Existing Markdown and HTML adapters and rich surface are in
  `lmd/package/editor`; the source surface already supports large files.
  `lmd/package/doc/doc_viewer.ls` lists the viewer's additional markup and
  property formats. Input-owned LI-029–LI-032 block JSON, TOML, YAML,
  iCalendar and vCard property editing: their parsers accept malformed input.
  The attempted compensations were removed under the cross-project rule;
  JSON, YAML and TOML retain their pre-existing source surfaces; iCalendar
  and vCard now also open as source. Java
  properties, INI, CSV/TSV and strict XML values share the property surface.
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
- Manual pages remain source-only. A trial using the shared markup adapter
  found that the input parser displays the zero-width `\\&` escape as `&`
  (input-owned LI-035). The Lambda `lambda.doc.misc` man writer also emits
  section headings as `.SH` followed by the heading on the next line, which
  reparses as an empty heading followed by a paragraph. The reproduction is
  `temp/edit-viewer/man-roundtrip.ls` and its log. Correct the writer's heading
  serialization in the owning package and rerun round-trip and edit/save
  cases after the input parser issue is fixed; do not admit a lossy adapter.
- Basic RTF paragraphs, emphasis and a single document-wide font have a rich
  adapter with a bounded writer and model round-trip check (D9.3.4). Ordinary
  ANSI font tables and font selection are retained in the source envelope and
  shown on the editing surface. Per-run fonts, non-ANSI font charsets, embedded
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
  `plain` remains usable when these preferences are present without a font
  override. Model and edit/save replays cover the retained header.
  Ordinary RGB color tables and per-run foreground colors map to the shared
  span-color marks; rich save retains the table and reconstructs color runs.
  Unsupported color metadata, invalid indices and colors outside the table
  remain source-only or are rejected on save. Model and UI tests cover
  mixed colors, emphasis, preservation, and invalid tables.
- `.txtl` uses the Textile rich adapter and `.ical`, `.1m` and `.3p` open in
  source mode. The Textile edit/save replay and XML attribute operations pass.
- SVG, graph, LaTeX, slides and diff suffixes now select the source surface;
  known binary media and containers are rejected even with `--source`.
- Save, conflict and close handling reuse the editor file-session module.
- Known binary media are rejected even with `--source`; a PDF header check
  also rejects ASCII PDFs using a text suffix. A byte comparison, strict
  UTF-8 validation and embedded-NUL check prevent unsafe text reads from
  being written back. Legacy-encoded files require conversion first.
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
  Calendar/contact source and RTF rich/fallback UI replays all pass.
- Next: after Input fixes LI-029–LI-032 and LI-034, implement property editing for the
  affected data formats without compensating for parser defects. After LI-033,
  use the provided source ranges for individually retained footnote blocks.
  Independently expand the rich RTF subset only with preservation evidence,
  and implement remaining markup adapters. Do not silently rewrite source or
  change sibling providers.
