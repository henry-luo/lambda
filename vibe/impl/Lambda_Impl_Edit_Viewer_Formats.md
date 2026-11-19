# Editing viewer document formats

## Editing admission ruling — 2026-11-16

D10.2.5 now requires graphical editing for successfully parsed documents in
formats assigned a graphical surface, and for documents with isolated syntax
errors when recovery yields a reliable remaining model. Error regions must be
read-only and survive saves unchanged; unrecoverable input uses source mode.
See [the working design](../radiant/Radiant_Design_Edit_Mode.md#21-parsed-documents-and-isolated-syntax-errors).
This is a design update, not an implementation claim: graphical fallback for
valid unsupported constructs and isolated-error recovery remain to implement.

## Active continuation — 2026-11-16

- Goal: rich man editing with retained header; YAML anchors/tags/directives,
  streams and non-string keys; TOML table-heading rename; multiple iCalendar
  roots. User previously authorized relevant Input/Radiant source changes.
  Preserve existing sibling work, including unrelated LaTeX changes.
- Input man `.TH` title now uses the first quoted argument, verified with the
  existing pre-fix `man-title-current.log` and owner tests `man-native.log`.
  The shared markup adapter admits `.man` and section suffixes and retains the
  original preamble and `.TH` line on edits; `man_format.ls` yields 8/8 and
  `man-ui.log` passes save/undo. It rejects edits that cannot retain the title.
- Input iCalendar now returns its historical map for one calendar and an ordered
  array for multiple roots, rejecting nested/malformed components. The property
  editor parses streams; `calendar-native.log` passes 3/3, `calendar_stream.ls`
  yields 6/6, and `doc.misc`/viewer now handle multiple roots (D9.3.4).
- TOML heading rename rewrites matching decoded path prefixes, including
  descendants and repeated array tables; collisions are rejected by reparse.
  `toml_table_rename.ls` passes 10/10 and `toml-man-ui.log` passes 2/2 replays.
  Existing TOML property model passes 22/22.
- YAML supports scalar/collection anchors and tags, aliases, directives,
  document streams including empty and inline-start documents, typed keys and
  supported explicit or implicit collection keys. Edits reparse the entire
  document and retain unchanged source (S7.5.1v6, D9.3.4). Input now scopes
  anchors per document, rejects duplicate typed/collection keys, preserves
  implicit collection keys and parses mappings after an inline document start.
  `yaml-stream-anchor-before.out` and `yaml-inline-start.out` retain pre-fix
  reproductions. Native controls pass 8/8 in `yaml-final-native.log`; all 276
  upstream YAML JSON comparisons pass in `yaml-corpus.log`. Indented multiline
  explicit keys, including empty keys, can be renamed or removed.
- The full editor sweep passes 215/215 fixtures, with 130/130 model tests and
  12 model plus five UI tests under each forced backend; the multiline-key UI
  replay also passes under both. The calendar viewer replay passes. Radiant
  baseline passes 8,170, with 297 partial and 20
  skipped. The release pin passes all 40,261 selected Test262 cases, with 2,652
  skipped and no regressions. The Lambda baseline was repeated against the final
  source snapshot and passes 7,548/7,548 (`lambda-baseline-audited.log`). Input's final
  `test-native` aggregate retains exactly three unrelated failures: the two
  LI-003 foreign-type contract tests and LI-002 external-parent test. Its YAML
  corpus passes 276/276 comparisons, 86/86 invalid-input no-crash controls and
  29/29 parse-only no-crash controls (`input-baseline-final.log`).
- The CLI and Input owner ledger describe the resulting behavior. Lambda HEAD is
  `315915449`; sibling Input and Radiant trees contain authorized prior
  uncommitted fixes plus unrelated Input LaTeX changes. Sequential gate evidence,
  source hashes and release pin are under `temp/edit-man-structured/`.

## Current checkpoint

- Updated 2026-11-15 in `/Users/henryluo/Projects/Lambda-opus`, branch `master`,
  HEAD `17ce141e1`; Input base `2672c1a`, Radiant base `9a21ad9`. The current
  prioritized implementation is complete and verified; changes are uncommitted
  across these three checkouts.
- The user explicitly authorized needed Radiant/Input fixes on 2026-11-15.
  This continuation implements RI-090 and LI-056–LI-058 and enables the four
  affected property formats. RTF, RST, Textile and EPUB expansion is deferred.
- Document processing and package policy follow D9.3.4/D10.2.1. Input owns
  validation; Radiant owns layout and canonical selection. Empty TOML keeps
  the existing null contract (S1.5), and parser failures remain acknowledged
  under S7.5.1v6. No formal ruling changed.

| Required behavior | Current implementation |
|---|---|
| Keyboard editing | Home/End scans the full block-local visual line and preserves canonical boundaries outside retained inline content. Shift retains the selection anchor. Wrapped spans, superscripts, image-only retained spans and inline blocks have native controls. |
| Structured data | YAML/TOML now have source-backed property trees, alongside JSON/XML/INI/properties. TOML table headings are retained; supported YAML block/flow collections and nested sequence objects are projected. Calendar/contact component trees retain folding, parameters and repeated fields. CSV/TSV remain editable tables. |
| Source fidelity | Shared quote/line/span helpers avoid duplicate scanners. Edits preserve comments and unchanged line endings, including normalized multiline controls. Replacement validation cannot introduce neighboring fields. Invalid/unsupported syntax falls back to source. |
| Other documents | Existing rich markup, source-based SVG/graph/scripts and non-text rejection remain in the full editor sweep. DOCX/man backlog and deferred rich formats remain outside this pass. |

- Supported YAML properties exclude references, tags, directives, document
  streams and non-string keys; those documents use source mode. Inline sequence
  mappings support first-field deletion and retain empty mapping/sequence kinds
  when their last child is removed. TOML table headers are not property-name
  controls. The single-calendar profile rejects extra roots; multiple vCards
  remain supported.
- Owner records RI-090 and LI-056–LI-058 are archived with before/after evidence.
  RI-091 remains open: its StateStore link is repaired with fail-fast declarations
  for services absent from that isolated test host, while its custom-layout
  portion is outside the repair.

| Verification | Latest result |
|---|---|
| Input focused parser controls | 7/7 pass; malformed/valid neighbors, Unicode/control strings and CRLF/CR block scalars (`validation/focused-final.log`) |
| Native StateStore | 39/39 pass, including retained image/inline-block controls that fail before repair (`ri090/{box-before,native-final}.log`) |
| Editor/RTF model selection | 123/123 pass (`validation/model-qualified.log`) |
| Edit UI selection | 209/209 fixtures; 212 GTests including scheduler controls (`validation/edit-qualified.log`) |
| Forced interpreter/JIT | Each passes three models and five UI flows (`validation/{model,ui}-{interp,jit}-final.log`) |
| New format models | YAML 32/32, TOML 22/22, RFC 13/13 boolean checks; all golden files are paired |
| Input aggregate | Audited final run has exactly three existing failures: the LI-003 foreign/nullable contract cases and LI-002 external-parent case. All 276 upstream YAML JSON comparisons pass. See `validation/input-baseline-audited.log`; this gate remains non-green. |
| Final consumer gates | Lambda 7,537/7,537; Radiant 8,170 passed, zero failed, 297 partial and 20 skipped; Test262 40,261 fully passing, zero regressions and 2,652 skipped. Evidence: `validation/{gates,final-gates}.json` and the referenced logs. |

- The fresh final release pin and `lambda.exe` are byte-identical, SHA-256
  `17466297e0c4d986bc7e3fa41dfdd86892ab3596652ebdb85ba3c2a8fb3e537b`.
  The repeated release build produces the executable already tested by Test262;
  no additional Test262 replay is needed. Source snapshots cover 32 Lambda,
  five Input and six Radiant code/test files. The final owner-only test
  correction does not change runtime sources or the release artifact.
  Evidence: `validation/{source-snapshot,final-verification}.json`.
- RI-090 reproduction: `ri090/before.log` fails two of three footnote assertions;
  the focused post-fix selection passes eight replays, including Shift+Home/End.
  Three existing fixtures assumed fragment-local navigation; their cursor setup
  now uses character movement to keep the same intended formatting/delete/undo
  assertions. References and tolerances are unchanged.
- YAML CRLF reproduction: `validation/newline-before.log`; seven owner controls
  pass after normalizing source line breaks. The additional TOML string probe
  initially returned four false rejection results (`toml-string-before.out`);
  the final release replay returns four true results (`toml-string-qualified.log`).
- Final YAML CRUD review: deleting the first inline field previously raised
  `this container requires source mode`; deleting a final nested mapping field
  produced `server:\n` and changed its kind to scalar/null. Both reproduce in
  `validation/yaml-{delete,empty-delete}-before.out`. Deletion now keeps the
  sequence marker and emits empty flow collections where needed. The expanded
  YAML model passes 32/32 checks and the new delete/undo UI passes 6/6 assertions
  (`yaml-model-delete.out`, `yaml-delete-ui.log`). The repeated final Lambda
  baseline passes 7,537/7,537 on this package change. Native provider behavior is unchanged by this last repair.
- Aggregate audit correction: `input-baseline-qualified.log` has a fourth
  failure, `TomlTests.TomlUnicodeSurrogatePairs`, beyond LI-002/LI-003. That old
  fixture accepts UTF-16 surrogate escapes, contrary to TOML's Unicode scalar
  rule (<https://toml.io/en/v1.0.0#string>). Its positive coverage was
  corrected to valid scalar/raw UTF-8 spellings, preserving byte assertions and
  adding explicit rejection checks. All ten Input/TOML units pass
  (`validation/toml-fixture-after.log`). The audited aggregate confirms only
  the three unrelated LI-002/LI-003 failures remain. No parser relaxation was
  made; the runtime/editor results and release pin remain applicable.

## Implementation history

- 2026-11-15 authorized owner repairs and structured editing: RI-090 and
  LI-056–LI-058 are fixed in their owners and archived with before/after
  evidence. `lambda.editor` now exposes source-backed YAML/TOML and RFC
  component/property trees, with explicit source fallback for unsupported
  syntax (D9.3.4, D10.2.1, S7.5.1v6). Shared span/quote/line-ending helpers
  serve JSON, INI and the new adapters. Final YAML deletion preserves empty
  collection kinds and sequence markers. The old TOML Unicode test's invalid
  positive fixture is replaced with standard scalar/raw UTF-8 spellings and
  explicit surrogate rejection, preserving byte assertions. The checkpoint
  records final source/release identity and focused/aggregate results.
  Deferred rich formats and older broad format backlog remain outside this
  prioritized continuation.


- 2026-11-15 optional Markdown footnotes: the parser omits the footer for
  documents without definitions. The adapter previously called `content()` on
  that absence and then caught the type error. A shared `parsed_footnotes`
  helper checks the optional footer before reading its children (S1.5), and
  import and definition validation use it. The six-case probe covers empty,
  ordinary and structured Markdown, editable notes, unreferenced definitions
  and retained unsupported definitions. Its outputs are identical before and
  after; runtime diagnostics fall from nine to zero in AUTO, interpreter and
  JIT (`temp/edit-viewer/optional-footnotes/verification.json`). Existing
  editor/RTF model tests pass 120/120 and edit UI passes 202/203 with only
  RI-090 failing (`optional-footnotes/{model,edit}.log`). No reference or
  diagnostic configuration was changed.
  Final Lambda, Radiant and Test262 gates pass on all current editing changes;
  their results and executable/source identity are in the current checkpoint.
- 2026-11-15 modal editing continuation: custom property/link/image dialogs
  share the file dialogs' keyboard scope (D10.2.1, S12.1.3). The prior Add
  Property and link replays both wrote the unsaved document when Cmd/Ctrl+S
  was pressed in their fields (`temp/edit-viewer/modal-editing/before.log`).
  Add Property also allowed the source-view shortcut to discard the open
  dialog. All dialogs now keep document shortcuts outside the draft, cycle
  Tab/Shift+Tab through their controls, confirm their primary action with
  Enter from a field, and cancel with Escape. Shift+Enter keeps a newline in
  multiline fields. The shared path replaces separate link/property Enter
  handlers. Toolbar-opened rich editing dialogs release document focus, and
  the image address field autofocuses; the image keyboard replay fails
  without those focus changes (`modal-editing/image-before-focus.log`) and
  passes with them (`modal-editing/image-after.log`).
  All three new workflows pass in forced interpreter and JIT modes
  (`modal-editing/interp.log`, `modal-editing/jit-final.log`), and the editor/RTF
  model selection passes 120/120 (`modal-editing/model.log`). The final edit UI
  sweep passes 202/203, with only the existing RI-090 replay failing
  (`modal-editing/edit-final.log`). An earlier UI sweep overlapped the JIT
  fixtures and is not used for qualification; the failing JIT link replay
  passed alone and in the subsequent sequential run. The aggregate baselines
  in the following checkpoint predate this modal extension.
- 2026-11-15 general keyboard continuation: Save As now handles Enter and
  Cmd/Ctrl+S through the typed destination instead of saving the original
  file (D9.3.4, S12.1.1v3). All three rich/source/property replays failed
  before the fix (`temp/edit-viewer/file-dialogs/before.log`); the originals
  contained the unsaved marker. The shared file-dialog handler owns keyboard
  submission, Escape and Tab cycling. Dialogs release document/control focus
  before autofocus; completion restores the surface, including the property
  editor. Sequential-focus ordering is extracted from the DOM behavior module
  into a helper without templates, so reuse does not register an HTML root
  view in the editor (D10.2.1). The three keyboard-only replays pass 26/26
  assertions each, including safe overwrite confirmation, focus cycling,
  undo and save-on-close (`file-dialogs/keyboard-final.log`); five focused
  DOM focus replays also pass (`file-dialogs/focus-regressions.log`).
  The editor/RTF model selection passes 120/120 (`file-dialogs/model-suite.log`),
  and the full edit UI selection passes 199/200 with only the existing
  RI-090 replay failing (`file-dialogs/edit-suite.log`). The Lambda baseline
  passes 7,534/7,534 (`file-dialogs/lambda-baseline.log`); Radiant reports
  8,170 passes, zero failures, 297 partial and 20 skipped, excluding that
  separate RI-090 replay (`file-dialogs/radiant-baseline.log`).
  Test262 reports 40,260 fully passing, one slow batch case passing on isolated
  retry, zero regressions and 2,652 skipped (`file-dialogs/test262-baseline.log`).
  The pinned release host has SHA-256
  `43a9b014723201b7c7f46cd28a2c805bedacd2ed478d2d4a106f42163b889bac`,
  identical to the freshly built release executable. The Test262 runner
  refreshed its generated `test/js262/t262_partial.txt` list in the shared
  test corpus; that output is outside the Lambda source diff.
- 2026-11-15 prioritized general editing issues: the RI-090 Markdown replay
  still fails its two edge-placement assertions, and the LI-056–LI-058
  probes still accept the malformed examples in the owner ledgers. YAML and
  TOML remain on the source surface: new UI replays edit and save malformed
  files while preserving their other lines, and routing checks cover `.yaml`,
  `.yml`, and `.toml`. Those two UI replays, the existing calendar/contact
  source replays, and the routing model test pass (D9.3.4). A candidate
  LI-058 Input patch and regression cases are in
  `temp/edit-viewer/li058-proposal/input.patch`; they have not been applied,
  compiled, or tested. Automatic approval review rejected applying them in
  the sibling checkout under the project ownership rule. RI-090 and
  LI-056–LI-058 remain open and need owner-side fixes before enabling their
  affected editing behavior.
- 2026-11-15 RTF table creation: the table toolbar creates rows of ordinary
  cells sized to the document's page width minus horizontal margins and gutter
  (D9.3.4). Omitted page settings use the defaults in the
  [RTF 1.7 specification](https://latex2rtf.sourceforge.net/RTF-Spec-1.7.pdf).
  The available width is split into integer twips without rounding loss;
  invalid or too-small page widths decline creation. A separating paragraph
  keeps newly inserted adjacent tables distinct on save/reopen. The schema
  follows the current envelope on initial open, source switching and reload.
  The model regression covers defaults, custom geometry, uneven division,
  invalid width and adjacent tables (`rtf-table/create-model.log`). Three
  focused UI replays pass, including reload (`rtf-table/create-ui-final.log`);
  the editor/RTF model selection passes 120/120 and three tests pass both
  execution tiers (`rtf-table/create-{model-suite,interp,jit}.log`, no
  interpreter fallback). The full edit UI selection passes 194/195, with
  only the existing RI-090 caret replay failing (`rtf-table/create-ui-suite.log`).
  The aggregate baselines recorded below predate this creation change.
- 2026-11-15 RTF table continuation: an ordinary two-row RTF table was
  reproduced as source-only (`temp/edit-viewer/rtf-table/before.log`); Pandoc
  renders it as two rows (`rtf-table/pandoc.html`). The shared reader now has
  an opt-in editor-table representation of rows, per-cell widths, character
  runs and paragraph settings. It rejects incomplete rows and multi-paragraph
  cells, while the default viewer projection and retained native oracle stay
  unchanged. Direct/grammar-reader, malformed-row and legacy-oracle checks
  pass in interpreter and JIT (`rtf-table/reader-{interp,jit}.log`). The
  editor now validates and imports simple rows into its table model, writes
  cell widths and formatting, and supports cell edits and undoable row/column
  insertion and deletion. New cells inherit adjacent widths and paragraph
  properties; inserting a column pads short rows. Enter in an inline cell
  makes a line break instead of splitting the cell. Merged cells, borders,
  multi-paragraph cells and other unsupported controls still use source mode;
  table creation was added in the later checkpoint above. Five table UI replays pass
  (`rtf-table/final-focused-ui.log`), and Pandoc independently renders the
  saved line break and table (`rtf-table/pandoc-enter.html`). The editor/RTF
  selection passes 119/119; three focused tests pass interpreter and JIT
  (`rtf-table/final-{model-suite,interp,jit}.log`, no interpreter fallback).
  The edit UI selection passed 191/192 with only the existing RI-090 replay
  failing (`rtf-table/final-ui-suite.log`); its run predated the added ragged
  table fixture, which passes separately. The Lambda baseline passes
  7,533/7,533 (`rtf-table/lambda-baseline.log`); Radiant reports 8,170 passes,
  no failures, 297 partial and 20 skipped (`rtf-table/radiant-baseline.log`),
  excluding the separate RI-090 replay. Test262 reports 40,257 fully passing,
  four passing on isolated retry after batch kills, zero regressions, and
  2,652 skipped (`rtf-table/test262.log`); it used the same pinned release
  executable as the preceding run. RTF table
  structure is described by the
  [RTF 1.7 specification](https://latex2rtf.sourceforge.net/RTF-Spec-1.7.pdf)
  and document preservation follows D9.3.4.
- 2026-11-15 RTF paragraph pagination continuation: the editor retains
  keep-together (`keep`), keep-with-next (`keepn`) and page-break-before
  (`pagebb`) as paragraph properties (D9.3.4). Microsoft documents these as
  paragraph properties and `pard` as resetting them in its
  [RTF control-word table](https://learn.microsoft.com/en-us/windows/win32/controls/about-text-object-model#tom-rtf).
  They affect saved RTF but not the unpaginated editing surface. The old
  source-mode refusal is in `temp/edit-viewer/rtf-keeps-before.log`. The model
  test covers inheritance, group scope, empty paragraphs, paragraph and
  character resets, both reader paths, no-op save, round trip and invalid or
  late controls. The final reader/reset assertions pass in both tiers
  (`rtf-keeps-final-{interp,jit}.log`) after the aggregate baselines below.
  The UI replay edits, saves,
  switches views and splits a paragraph (`rtf-keeps-ui-split.log`, 5/5).
  Three focused tests pass in both interpreter and JIT, with no interpreter
  fallback (`rtf-keeps-{interp,jit}.log`); the editor/RTF model selection
  passes 117/117 (`rtf-keeps-model-suite.log`). The edit UI selection passes
  187/188, with only the existing RI-090 caret replay failing
  (`rtf-keeps-ui-suite.log`). The Lambda baseline passes 7,531/7,531
  (`rtf-keeps-lambda.log`). Radiant reports 8,170 passes, no failures, 297
  partial and 20 skipped (`rtf-keeps-radiant.log`), excluding the separate
  RI-090 replay. The Test262 selection reports 40,257 fully passing, four
  passing on isolated retry after batch kills, zero regressions, and 2,652
  skipped (`rtf-keeps-test262.log`). It used the pinned release executable
  from the preceding continuation; native sources have not changed.
- 2026-11-15 table row continuation: CSV and TSV grids can insert an empty,
  full-width row before any existing row as one undoable edit (D9.3.4).
  The new row takes the target record's ending, or the preceding record's
  ending when the target is unterminated; embedded quoted newlines do not
  count as record endings. Existing fields and delimiter-detection safeguards
  are retained. Model coverage exercises mixed endings, quoted multiline
  fields, ragged rows and TSV in both interpreter and JIT with no interpreter
  fallback (`temp/edit-viewer/row-final-{interp,jit}.log`). Four CSV/TSV row
  and column UI replays pass after the final change (`row-final-ui.log`),
  including a pending cell edit and undo. The broader model selection passed
  116/116 before the final line-ending refinement (`row-model-suite.log`).
  The edit UI suite ran across that refinement and passed 186/187, with only
  the existing RI-090 caret replay failing (`row-ui-suite.log`). The Lambda
  baseline passes 7,530/7,530 (`row-lambda-baseline.log`); Radiant reports
  8,170 passes, no failures, 297 partial and 20 skipped
  (`row-radiant-baseline.log`), excluding the separate RI-090 replay. Test262
  passes 40,261/40,261 selected cases with no retries or regressions and 2,652
  skipped (`row-test262.log`). Its release executable was copied and pinned
  (SHA-256 `43a9b014723201b7c7f46cd28a2c805bedacd2ed478d2d4a106f42163b889bac`)
  so stale-process cleanup could not affect a separate checkout.
- 2026-11-15 Markdown footnote-definition continuation: ordinary definitions
  are editable as rich blocks, including multi-paragraph content, while their
  original source is retained until that definition changes (D9.3.4). A
  definition with unsupported content stays view-only. The model regression
  covers source retention and an edited round trip; the focused UI replay
  covers editing, save, undo and source switching. After updating the prior
  view-only expectations, the editor/RTF model selection passes 117/117 and
  the edit UI suite passes 184/185; the existing RI-090 caret replay remains
  failing (`temp/edit-viewer/commit-{model,ui}.log`). The aggregate baselines
  recorded below predate this footnote-definition change.
- 2026-11-15 RTF highlight continuation: the shared direct/grammar reader
  projects RTF 1.7 `highlight` color-table indices to text backgrounds; zero
  clears the highlight and group scope is retained. The rich editor maps
  highlights into its existing span marks and writes supported indices back
  with the retained color table (D9.3.4). It rejects invalid indices and
  unrepresentable colors. The [RTF 1.7 specification](https://latex2rtf.sourceforge.net/RTF-Spec-1.7.pdf)
  defines the color-table index and distinguishes highlighting from character
  formatting, so `plain` does not clear it. The prior source-mode refusal is
  captured in `temp/edit-viewer/rtf-highlight-before.log`; model coverage
  checks both readers, scoped and reset formatting, mixed foreground colors,
  no-op save and rich round trip. The UI replay edits inside a highlighted
  run, saves and switches through source and back (`rtf-highlight-ui-final.log`,
  8/8 assertions). The editor/RTF model selection passes 116/116; three
  focused tests pass interpreter and JIT (`rtf-highlight-{model-suite,tier-*}.log`).
  The edit UI suite passes 183/184; the remaining failure is the known RI-090
  replay (`rtf-highlight-ui-suite.log`). Lambda baseline passes 7,531/7,531
  (`rtf-highlight-lambda.log`). Radiant's baseline reports 8,170 passes,
  zero failures, 297 partial and 20 skipped; it does not include the separate
  RI-090 edit replay (`rtf-highlight-radiant.log`). Test262's first run
  overlapped another Lambda baseline; its default stale-process cleanup killed
  two `lambda.exe` processes which may have belonged to that run. After the
  other baseline finished, a rebuilt release binary was pinned via
  `LAMBDA_TEST262_EXE` (SHA-256 `e6be0cdd2302fb42ffd426b64b83efbfadd65716cb008f7e26d1eeb350eaa6c9`)
  and the baseline selection rerun without that cleanup. It reports 40,257
  fully passing, four slow cases passing on isolated retry, zero regressions,
  and 2,652 skipped; no batch was killed
  (`rtf-highlight-test262-pinned.log`).
- 2026-11-15 CSV/TSV column continuation: the grid can insert a column before
  any existing column or append one, and remove a column across all rows.
  Each operation is one undoable transaction; insertion pads short rows and
  removal refuses to eliminate a row's only field. Source spans retain quoted
  and multiline fields and line endings; the existing CSV delimiter check
  rejects changes that would reinterpret rows after reopening (D9.3.4).
  `delimited_columns.ls` covers ragged rows, CRLF, embedded newlines, TSV and
  delimiter rejection; CSV and TSV UI replays cover pending fields, undo and
  rejected removal (`temp/edit-viewer/columns-{model,ui,tsv-pending}.log`).
  The editor/RTF model selection passes 115/115, two focused model tests pass
  in interpreter and JIT tiers, and the edit UI suite passes 182/183 with
  the existing RI-090 caret replay failing (`columns-{model-suite,tier-*,
  ui-final}.log`). Lambda baseline passes 7,530/7,530 (`columns-lambda.log`).
  Radiant baseline reports 7,963 passes, one failure, 293 partial and 19
  skipped because the render runner exited without a summary; the earlier
  missing-monitor Scene3D case passed. The isolated render rerun passed 207,
  with four expected failures and one skip (`columns-radiant.log`,
  `columns-render.log`). The aggregate failure's cause is unconfirmed.
  Test262 passes 40,261/40,261 selected cases, with zero retries or regressions
  and 2,652 skipped (`columns-test262.log`).
- 2026-11-15 INI control-value continuation: INI skips only spaces and tabs
  after a separator, so leading form feed and vertical tab are significant
  value bytes. The property scanner now uses the same rule for value spans
  and multiline quote detection. Values containing control characters that
  do not display reliably in a field use an editable JSON string literal;
  no-op saves retain the original source and edits validate the literal before
  replacing the value. This follows D9.3.4. The viewer/editor mismatch was
  reproduced in `temp/edit-viewer/ini-space-probe.log`; model coverage also
  checks multiline and DEL values. The focused UI replay passes all seven
  assertions (`ini-control-ui-focused.log`). The editor/RTF model selection
  passes 114/114; the edit suite passes 180/181 with the existing RI-090
  caret replay still failing (`ini-control-{model-suite,ui-suite}.log`).
  Three focused model tests pass in forced interpreter and JIT modes
  (`ini-control-tier-{interp,jit}.log`). The Lambda baseline reports 6,732
  passes and three executable timeouts, after unusually long simultaneous
  stalls. The JS optimization and security executables pass isolated reruns
  (122/122 and 98/98); the JS executable passes 576/577, with a reproducible
  media playback stall recorded in the [Lambda ledger](../Lambda_Issue_Ledger.md#unattributed-dom-media-playback-stall--2026-11-15).
  The Radiant baseline reports 8,169 passes, one failure, 297 partial and 20
  skipped: `Scene3dPlatform.NativeHostCleanupRetainsTheApplicationRuntime`
  reproduces in isolation with zero GLFW monitors. Its layout selection has
  zero failures, and the separate edit UI replay above remains outside it.
  Test262 reports 40,259 fully passing, two slow cases passing on isolated
  retry, zero regressions and 2,652 skipped. One stalled batch was killed;
  all 104 lost results were recovered in the batch retry. Aggregate and isolated logs
  are under `temp/edit-viewer/ini-control-*`; none of these results establish
  the cause of the unrelated stalls or the missing monitors.
- 2026-11-15 Markdown footnote-reference continuation: references in ordinary
  paragraphs, headings, lists and tables can now be retained as inline atoms,
  allowing surrounding text edits while definitions remain as authored source.
  Formatting around references, deletion and undo have focused model and UI
  coverage. Blocks combining a reference with raw HTML remain view-only to
  retain the paired HTML projection. The before/after evidence is in
  `temp/edit-viewer/md-footnote-{inline-before,inline-ui-before,variants,
  marked-ui,delete-ui}.log`. Home and End at a reference on a visual line edge
  still put text on the wrong side of the atom; the independent plain
  contenteditable reproduction and native cause are recorded in Radiant
  [RI-090](../Radiant_Issue_Ledger.md#ri-090). The
  regression `test/ui/edit/edit_md_footnote_edges.json` remains failing until
  the owning Radiant fix; do not claim complete keyboard support. The full
  editor/RTF model selection passes 112/112; the full edit UI selection has
  179 passes and that one failure (`md-footnote-full-{model,ui}.log`). Three
  focused model tests pass forced interpreter and JIT with no interpreter
  fallback (`md-footnote-tier-{interp,jit}.log`). Atomic-edge text anchors
  share the existing RTF mechanism. D9.3.4 governs source preservation and
  D10.2.1 editor selection. The first Lambda baseline had one stale adapter
  golden for the newly editable paragraph; the updated golden asserts the
  inline reference and the focused rerun passes (`md-adapter-failure.log`,
  `md-adapter-updated.log`). The Lambda baseline rerun passes 7,528/7,528 with
  the Input owner's in-worktree LI-060 fix (`md-footnote-final-lambda.log`).
  Radiant's baseline selection reports 8,170 passes, zero failures, 297 partial
  and 20 skipped (`md-footnote-final-radiant.log`); it does not include the
  separate failing edit replay. Test262 passes 40,261/40,261 selected cases,
  with no retries or regressions and 2,652 skipped (`md-footnote-final-test262.log`).
- 2026-11-15 JSON control-name continuation: JSON object names containing
  NUL, line endings, tabs or other control characters now open in the
  property tree. Those names display their quoted JSON spelling, preserving
  the original spelling on a no-op; editing validates a complete JSON string
  before renaming. The Add Property dialog accepts newlines in names via
  Shift+Enter. Model and UI coverage includes control names, invalid drafts,
  no-op and edited saves, and transition back to an ordinary name. The former
  source-fallback replay now edits the property; a new add-dialog replay
  failed before the change (`temp/edit-viewer/json-{control-before,add-
  multiline-before}.log`). The full editor/RTF model selection passes
  111/111 and the edit UI suite 176/176 (`json-control-final-{model,ui}.log`).
  Two focused model tests pass in forced interpreter and JIT with no
  interpreter fallback (`json-control-tier-{interp,jit}.log`). JSON source
  preservation follows D9.3.4. The Lambda baseline passes 7,527/7,527 with the
  Input owner's in-worktree LI-060 fix (`json-control-lambda.log`). Radiant
  reports 8,170 passes, zero failures, 297 partial and 20 skipped
  (`json-control-radiant.log`). Test262 passes 40,261/40,261 selected cases
  with no retries or regressions and 2,652 skipped (`json-control-test262.log`).
- 2026-11-15 CSV delimiter continuation: editing a first-record field can no
  longer make the viewer redetect a different separator. The editor quotes a
  changed field containing the competing delimiter and compares row values
  against a fresh delimiter detection after structural edits. If the latter
  would reinterpret existing rows, the edit is refused; if row values are
  unchanged, the editor adopts the newly detected separator. TSV remains
  explicitly tab-delimited. The regression initially reproduced both silent
  reinterpretation and unsafe first-row deletion (`csv-detection-before.log`);
  model coverage exercises both delimiter directions, insertion, deletion,
  viewer parsing and empty files. The grid UI replay verifies saved bytes,
  source switching and rejected deletion (`csv-detection-ui.log`). The full
  editor/RTF model selection passes 110/110 and the edit suite 174/174
  (`csv-detection-full-{model,ui}.log`); three focused tests pass forced
  interpreter and JIT with no interpreter fallback (`csv-detection-tier-
  {interp,jit}.log`). Source-preserving document editing follows D9.3.4.
  The Lambda baseline passes 7,526/7,526 with the Input owner's in-worktree
  LI-060 fix (`csv-detection-lambda.log`). Radiant reports 8,170 passes, zero
  failures, 297 partial and 20 skipped (`csv-detection-radiant.log`). Test262
  passes 40,261/40,261 selected cases, with no retries or regressions and
  2,652 skipped (`csv-detection-test262.log`).
- 2026-11-15 document-package continuation: added an internal ZIP member
  replacement helper and an EPUB chapter adapter that reuses the existing
  XHTML rich model (D9.3.4, D10.1.4). The adapter checks round-trip safety,
  rejects changes to signed publications, and returns the original archive
  on a no-op. A sample chapter edit reopens in the EPUB reader; independent
  Python ZIP/XML inspection confirms only the chapter bytes changed, the
  EPUB mimetype is first and stored, and other member bytes match
  (`temp/edit-viewer/epub-chapter-oracle.log`). The ZIP helper retains the
  changed member's mode. Input [LI-063](../Lib_Input_Issue_Ledger.md#li-063)
  records that constructed replacement files cannot retain their original
  ZIP timestamps, and reconstructed directories also lose metadata. This
  blocks a metadata-preserving package save. `lambda edit` does not yet use
  these internal adapters: binary session lifecycle, chapter navigation,
  signatures and complete package preservation remain open. The editor,
  DOCX, EPUB and RTF model selection passes 136/136
  (`temp/edit-viewer/package-final-model.log`). Both new tests pass in forced
  interpreter and JIT modes without interpreter fallback
  (`package-tier-{interp,jit}.log`). The Lambda baseline passes 7,525/7,525
  with the Input owner's in-worktree LI-060 fix (`package-final-lambda.log`).
  Radiant reports 8,170 passes, zero failures, 297 partial and 20 skipped
  (`package-final-radiant.log`). Test262 passes 40,261/40,261 selected cases
  with no retries or regressions and 2,652 skipped (`package-final-test262.log`).
- Outstanding document-package coverage: `lambda view` opens DOCX and EPUB,
  but `lambda edit` currently rejects both by suffix. These are document
  containers, so media rejection alone does not establish that the original
  editing objective is met. DOCX import renders to HTML and its existing
  exporter explicitly creates a new package without retaining unrecognized
  parts; it cannot be used as a lossless edit/save path. Investigate a
  package-preserving adapter and the corresponding EPUB path before claiming
  those formats supported. The existing Input-owned issues below remain open.
- 2026-11-15 reload continuation: opening and reloading share the same media,
  UTF-8, NUL and byte-preservation checks; reload also checks the selected
  format's round trip before discarding the existing edit. A successful reload
  retains the session's rich/source switch metadata. The procedural regression
  reproduced acceptance of NUL, invalid UTF-8 and PDF content; the UI replay
  reproduced loss of the unsaved source edit on a non-text reload. A second
  UI replay reproduced loss of the rich-view switch after valid reload. These
  cases pass after the fix (`temp/edit-viewer/reload-{safety-before,safety-final,
  ui-before,ui-after,rich-before,rich-after}.log`). CSV replays additionally
  verify edits retain CRLF within a cell and an empty grid can receive its
  first row. The CRLF fixture now has an explicit Git attribute so those bytes
  survive a fresh checkout. The editor/RTF model selection passes 107/107 and
  the edit UI suite passes 172/172 (`reload-final-{model,ui}.log`). Two focused
  tests pass in forced interpreter and JIT modes without interpreter fallback
  (`reload-tier-{interp,jit}.log`). File effects and source preservation follow
  S12.1.1v2 and D9.3.4. The Lambda baseline passes 7,523/7,523 with the Input
  owner's in-worktree LI-060 fix (`reload-final-lambda.log`). Radiant reports
  8,170 passes, zero failures, 297 partial and 20 skipped
  (`reload-final-radiant.log`). Test262 passes 40,261/40,261 selected cases,
  with zero retries or regressions and 2,652 skipped (`reload-final-test262.log`).
- 2026-11-15 INI continuation: source-backed property editing now reads quoted
  multiline values, section headers with trailing comments, and CR-only files.
  Quoted values use a multiline control; newly quoted values can be expanded
  after committing. Edits retain surrounding comments and existing line
  endings, and added value lines follow the edited record in mixed-ending
  files. Invalid or unterminated values remain rejected. The model and UI
  failures were reproduced before implementation, including mixed endings
  (`temp/edit-viewer/ini-multiline-{before,ui-before}.log`,
  `ini-mixed-before.log`). The editor/RTF model selection passes 106/106 and
  the edit UI suite passes 168/168 fixtures (`ini-final-{model,ui}.log`).
  Three relevant model tests pass in forced interpreter and JIT modes, with
  no interpreter fallback (`ini-final-tier-{interp,jit}.log`). The CRLF UI
  fixture is protected from Git line-ending conversion. Source preservation
  follows D9.3.4. The Lambda baseline passes 7,522/7,522 with the Input
  owner's in-worktree LI-060 fix (`ini-final-lambda.log`). The Radiant
  baseline reports 8,170 passes, zero failures, 297 partial and 20 skipped
  (`ini-final-radiant.log`). Test262 passes 40,261/40,261 selected cases with
  no retries or regressions and 2,652 skipped (`ini-final-test262.log`).
- 2026-11-15 properties continuation: the property view now scans Java
  properties by logical record, including continued keys and values, bare
  keys, comment boundaries and odd/even trailing backslashes. Values show
  properties-source escape spelling without continuation indentation;
  edits retain unchanged continuation spans when valid and reserialize only
  the edited value when a changed escape would break the record. LF, CRLF,
  CR, blank continued lines, end-of-file continuations, insertion, rename,
  deletion and NUL rejection have model coverage. A 60-case model matrix
  compares edited values and neighboring entries with the Input parser.
  The CRLF UI replay verifies exact saved bytes; its fixture has a targeted
  `.gitattributes` exception. Oracle's [Properties.load specification](https://docs.oracle.com/en/java/javase/21/docs/api/java.base/java/util/Properties.html#load(java.io.Reader))
  defines logical lines and backslash parity; source preservation follows
  D9.3.4. Focused tests and UI replays pass (`temp/edit-viewer/properties-
  {cont-before,cont-blank-after,cont-crlf-ui,nul-after}.log`). The full
  editor/RTF model selection passed 105/105 and the edit suite passed 166/166
  fixtures (`properties-final-{model,ui}.log`). Three focused property model
  tests pass forced interpreter and JIT, with no interpreter fallback
  (`properties-cont-tier-{interp,jit}.log`). The Lambda baseline passed
  7,521/7,521 with the Input owner's in-worktree LI-060 fix
  (`properties-final-lambda.log`). The Radiant baseline passed with 8,170
  passed, zero failures, 297 partial and 20 skipped
  (`properties-final-radiant.log`). Test262 passed 40,261/40,261 selected
  cases, with no retries or regressions and 2,652 skipped
  (`properties-final-test262.log`). Earlier notes saying continued properties
  open in source mode are superseded.
- 2026-11-15 inline page-break continuation: RTF `page` controls now remain
  inline atoms in their actual paragraph, including at the start or end and
  for consecutive breaks. Empty text leaves provide caret positions at atomic
  edges; they do not add source characters. Paragraph formatting after a
  leading break remains effective. The default viewer still uses its existing
  block projection. Page-only and inline edit/save replays pass, as do
  Backspace, forward Delete and undo. The shared editor deletion command now
  handles adjacent text runs, atoms and native target ranges at atom edges;
  model regressions reproduced both the collapsed-caret and native-range
  failures before the fix. Six focused contenteditable/editor UI replays pass
  (`temp/edit-viewer/rtf-inline-page-{before,both-ui}.log`,
  `rtf-page-only-{before,both}.log`, `inline-delete-{before,target-before,
  forward-before,both-after,editor-ui}.log`). The full editor/RTF model
  selection passed 104/104 and the full edit suite passed 164/164 fixtures
  (`rtf-page-anchor-full-{model,ui}.log`). The Lambda baseline passed
  7,520/7,520 with the Input owner's in-worktree LI-060 fix
  (`rtf-page-final-lambda.log`). The Radiant baseline passed with 8,170 passed,
  zero failures, 297 partial and 20 skipped (`rtf-page-final-radiant.log`).
  Test262 passed 40,261/40,261 selected cases with no retries or regressions
  and 2,652 skipped (`rtf-page-final-test262.log`). Three focused page-break
  and inline-deletion model tests also pass forced interpreter and JIT runs;
  interpreter admission reports no fallback (`rtf-page-tier-{interp,jit}.log`).
  This supersedes the earlier source-only inline-page note below. Source
  preservation follows D9.3.4.
- 2026-11-15 RTF continuation: `lang` character-run language IDs are retained
  through edits, including nested groups, combined font/color/small-capital
  marks and `plain` resets to `deflang`; invalid or absent parameters still
  fall back to source. The editor keeps the language ID as model metadata rather
  than presenting a language control. The model regression initially failed
  because the control was source-only. Ten focused RTF/package tests pass,
  as does a UI replay that edits inside the language run, saves, and switches
  through source and back. The full editor/RTF model selection passed 102/102
  and the full edit UI suite passed 161/161 fixtures
  (`temp/edit-viewer/rtf-run-language-{before,focused,inside-ui,full-model,full-ui}.log`).
  The current Lambda baseline passed 7,518/7,518 with the Input owner's
  in-worktree LI-060 fix (`temp/edit-viewer/rtf-language-lambda-baseline.log`).
  The Radiant and Test262 baselines below predate this change.
  [Microsoft's character-format table](https://learn.microsoft.com/en-us/windows/win32/controls/about-text-object-model)
  defines the `plain` reset; source preservation follows D9.3.4.
- 2026-11-15 keyboard continuation: CSV/TSV cells use Cmd/Ctrl+Enter to
  commit and move down in the same column, or Shift+Cmd/Ctrl+Enter to move up;
  Enter remains available for multiline values. At an uneven or boundary row,
  the edit is committed without moving. Focused CSV and TSV replays cover
  multiline quoting, uneven rows, undo, and existing Tab navigation. A separate
  HTML table replay confirms context commands target the right-clicked cell
  even when another cell was previously selected. The new vertical replay
  failed before implementation; all six focused replays and the full edit UI
  suite (160/160 fixtures) pass afterward (`temp/edit-viewer/csv-vertical-
  {before,focused,full-ui}.log`). The aggregate baselines below predate this
  keyboard-only change. Source-preserving editing follows D9.3.4.
- 2026-11-15 follow-up: Input [LI-056](../Lib_Input_Issue_Ledger.md#li-056)
  blocks TOML property editing because the parser accepts invalid escapes,
  numbers and duplicate keys. [LI-057](../Lib_Input_Issue_Ledger.md#li-057)
  blocks YAML property editing because malformed scalars, aliases and flow
  collections can be accepted. Python `tomllib` and local `js-yaml` reject the
  reproduced inputs, respectively. [LI-058](../Lib_Input_Issue_Ledger.md#li-058)
  blocks calendar/contact property editing: unmatched boundaries can be accepted
  and calendar events silently dropped. [LI-059](../Lib_Input_Issue_Ledger.md#li-059)
  records remaining RST literal code-block newline changes after LI-047; RST
  literal blocks remain source-only. [LI-061](../Lib_Input_Issue_Ledger.md#li-061)
  blocks broadening Textile rich admission: its parser loses styled span
  attributes, which otherwise look like the harmless wrappers added by LI-045.
  [LI-062](../Lib_Input_Issue_Ledger.md#li-062)
  blocks rich man output because the Input reader retains `.TH` argument quotes
  in the visible title; independent `mandoc` gives the same title for quoted
  and unquoted forms. Preserving the original man header is a separate editor
  requirement after the provider fix (`man-title-parse.log`, `man-title-*.html`).
  Source-preserving editing is governed by
  D9.3.4 and parse errors by S7.5.1v6. Probes are under `temp/edit-viewer/`.
  Independently, the RTF reader and rich surface now preserve nonnegative
  paragraph spacing before/after (`sb`/`sa`), including inherited settings and
  `pard` resets. The new model regression failed before the change and passes
  after it; direct and grammar readers agree, and the focused RTF tests and
  spacing/indent edit-save replays pass (`rtf-spacing-{before,after,focused,ui}.log`).
  Independent macOS `textutil` shows inherited and reset margins for the same
  sample (`rtf-spacing-oracle.html`); its HTML uses 15/7.5 px for 300/150
  twips while the editor uses 20/10 CSS px at 96 dpi.
  RTF document-level page dimensions, margins, gutter, default tab width and
  landscape orientation are retained across edits; late, nested, invalid or
  parameterized flag controls still fall back to source. Page settings do not
  affect the unpaginated rich surface. The page-setup model reproduced the
  former fallback and passes with the page-setup UI replay; all 11 focused RTF
  tests and four spacing/page/indent/language replays pass (`rtf-page-{before,
  after}.log`, `rtf-spacing-page-{focused,ui}.log`).
  The full edit UI suite passed 153/153 fixtures and the editor/RTF model
  selection passed 99/99 (`rtf-continuation-full-{ui,model}.log`). The Lambda
  baseline passed 7,508/7,515; all seven failing `proc_sync_*` cases are in
  the preceding run's 29-failure set (`rtf-continuation-lambda-baseline.log`).
  The Radiant baseline passed with 8,170 passed, zero failures, 297 partial
  and 20 skipped (`rtf-continuation-radiant-baseline.log`). Test262 exited 0:
  40,259 fully passed, two passed only after isolated retries of batch kills,
  zero regressions, and 2,652 skipped (`rtf-continuation-test262-baseline.log`).
  After those baselines, paragraph-boundary RTF page breaks gained a marked
  separator in the rich model and retain empty and consecutive breaks across
  saves. An inline `page` control remains source-only because the reader
  projects it as separate blocks, which cannot safely retain one paragraph's
  semantics. The [RTF 1.7 specification](https://latex2rtf.sourceforge.net/RTF-Spec-1.7.pdf)
  distinguishes paragraph termination from required page breaks. The regression
  failed before the change; all 12
  focused RTF tests and four page/spacing/fallback UI replays pass
  (`rtf-page-break-{before,after,focused,ui}.log`). The editor/RTF selection
  passed 100/100 and the repeated full edit suite passed 155/155 fixtures
  (`rtf-page-break-full-{model,ui-retry}.log`). The first UI suite had one CSV
  replay killed after both assertions passed; three isolated repetitions and
  the full-suite retry pass. The aggregate baselines above predate page breaks.
  RTF small capitals (`scaps`) are now projected as vetted `font-variant` marks
  and reconstructed on save, including combined bold, font, size and color
  runs, group scope and `plain` resets. Ordinary `caps` controls remain
  source-only. The model reproduced the old fallback and passes afterward;
  all 13 focused RTF tests and the small-capitals/page-break/spacing UI replays
  pass (`rtf-smallcaps-{before,after,focused,ui,mixed}.log`). The package reader
  and grammar reader agree; the local Cocoa HTML exporter did not render the
  control, so it is not claimed as a visual oracle. The full editor/RTF model
  selection passed 101/101 and the edit UI suite passed 156/156 fixtures
  (`rtf-smallcaps-full-{model,ui}.log`); aggregate baselines predate it.
  Separate package issue: the reader maps RTF `caps` to small-capitals CSS,
  but the RTF specification distinguishes `caps` and `scaps`. The existing
  native oracle encodes the current behavior, while the local Cocoa exporter
  ignores the probe. Keep `caps` source-only in the editor until the reader's
  compatibility contract and independent reference are established; do not
  change the native oracle without the required scoped authorization.
  The proposed RTF highlighting
  extension was not admitted: Microsoft describes `highlight` as not being a
  character format, so its interaction with `plain` needs an independent
  compatible reader oracle before changing preservation behavior. Apple
  `textutil` and the installed Pandoc both ignore the highlight in the local
  probe (`rtf-highlight-oracle.rtf`), so neither establishes that behavior.
  The later full Lambda baseline reported 18 failures, including 16 MIR
  emission cases whose shared saved log subsequently showed 595/595 passing,
  and two forced-GC cases: one saw `lambda.exe` missing during spawn, and one
  exited 137. Their cause is not established. A separate 595-test emission
  run and both exact GC cases passed with unchanged executable hashes
  (`rtf-final-{emission-isolated,gc-isolated}.log`,
  `rtf-final-isolated-{before,after}.sha256`); the original GC log is retained
  as `rtf-final-gc-stress-original.log`. The Input owner concurrently fixed
  LI-060 in its worktree and this baseline's 2,392 Lambda runtime cases passed.
  The isolated-results Lambda baseline rerun passed 7,517/7,517, including
  the earlier sync failures, with the Input owner's uncommitted LI-060 fix
  present (`rtf-final-lambda-isolated.log`). Sampled executable and Input
  archive hashes matched during and after the run; the earlier aggregate
  failures remain recorded above. The current isolated-results Radiant
  baseline passed with 8,170 passed, zero failed, 297 partial and 20 skipped
  (`rtf-final-radiant-isolated.log`). The current Test262 baseline passed
  40,261/40,261 selected cases with zero retries or regressions and 2,652
  skipped (`rtf-final-test262.log`).
- 2026-11-15 continuation, Lambda `e55450ec8` plus this work: JSON now uses
  a source-backed property adapter under D9.3.4. Objects and arrays support
  scalar edits, key renames, insertion and deletion; literal fragments and
  whole candidates are parsed independently so edits cannot inject adjacent
  members. Unchanged source spelling, duplicate names, whitespace and line
  endings are retained. Invalid JSON and names containing NUL or line breaks
  fall back to source because the single-line key control cannot retain them.
  Existing XML source and node helpers moved into the shared source-text module.
  The focused JSON model tests and six UI replays pass, including invalid
  drafts, source fallback, switching and save/undo. The full editor/RTF model
  selection passed 97/97 and the edit UI suite passed 150/150 fixtures before
  the additional name-control guard; its targeted JSON and XML model and UI
  replays pass afterward. Evidence: `temp/edit-viewer/json-{full-model,full-ui,
  key-focus,final-ui}.log`. The Lambda baseline passed 7,484/7,513 with exactly
  the same 29 `proc_sync_*` failures as the preceding baseline
  (`temp/edit-viewer/json-lambda-baseline.log`). The Radiant baseline passed
  with 8,170 passed, zero failed, 297 partial and 20 skipped
  (`temp/edit-viewer/json-radiant-baseline.log`). Test262 passed 40,261/40,261
  selected tests with zero regressions and 2,652 skipped
  (`temp/edit-viewer/json-test262-baseline.log`).
- 2026-11-14 continuation, Lambda `09a40dfe2` plus this work. Java properties
  now scan unescaped key delimiters and use the repaired Input parser (LI-051)
  to decode names. Renaming and insertion escape Java key syntax; value edits
  retain the original key spelling. Continued lines still open as source. CSV
  delimiter detection now skips quoted fields, doubled quotes and embedded
  line breaks before counting delimiters, matching the Input LI-042 repair.
  Escaped-key and quoted-CSV model regressions both failed before the changes;
  edit/save UI replays cover both, and the invalid-keyboard replay now checks
  an invalid value since spaces are valid in property names. Source-backed
  editing is governed by D9.3.4. The editor/RTF model selection passed 96/96,
  and the full edit UI suite passed 146/146 fixtures (149/149 including its
  scheduler checks). The standard Lambda baseline passed 7,483/7,512: the 29
  failures are exactly the same `proc_sync_*` cases as the previous baseline,
  whose cause remains unconfirmed in the sync plan. Evidence is in
  `temp/edit-viewer/property-csv-{model-final,ui-final,lambda-baseline}.log`.
  Test262 separately passed 40,261/40,261 selected tests with zero regressions
  and 2,652 skipped (`temp/edit-viewer/property-csv-test262-baseline.log`).
- 2026-11-13; `Lambda-opus`, branch `master`, base `8b5ebe56e` plus
  uncommitted changes for this task. The goal is structured data editing,
  keyboard-oriented rich markup editing, source editing for other text,
  and rejection of non-text media.
- S12.1.1v2 and S12.1.3 govern file effects and editor state. Preserve the
  existing adapter round-trip checks and atomic save/conflict handling.
- Markdown footnotes now use the Input parser's repaired LI-041 source ranges
  to show individual definitions in authored order while preserving their
  exact source (D9.3.4). Duplicate and multiline definitions and intervening
  link definitions have model coverage. The two previously failing view-only
  UI replays pass. The math-scroll fixture's former 5.5 px width came from
  the earlier math layout, before null-delimiter spacing; the current SVG is
  0.54925 em, or 8.8 px at 16 px. Independent Chromium measured 8.78 px and
  a 4.25 px paragraph-relative top versus Radiant's 3.2 px; the fixture now
  retains its 2 px tolerance around the current geometry. TeX's 10 pt oracle
  confirms 2.4 pt of null-delimiter spacing. The three requested replays pass
  (`temp/edit-viewer/markdown-three-fixed.log`, `math-browser.log`,
  `math-reference-radiant.log`, and `tex-oracle/run.log`). The Input fixes
  for LI-046 and LI-048 also make two Org cases rich-editable; their replays
  now verify editing and saving rather than the superseded source fallback.
  The edit UI suite passed 144/144 and the selected edit/editor model suite
  passed 108/108. The Lambda baseline passed 7,481/7,510: all 29 failures
  were `proc_sync_*`; the isolated repository failure reports E407 at JSON
  column 8192, with cause unconfirmed and recorded in the sync plan. Evidence:
  `temp/edit-viewer/markdown-final-{ui,model,lambda-baseline}.log`.
  The Radiant baseline passed 8,167 tests, with three failures, 297 partial
  and 20 skipped: `PaintOwnership.HtmlBoxAdmissionFallsBackForChangedAndUnsupportedPaint`,
  `MotionCascadeTest.StylesheetResourcePrefetchPreservesAuthoredUrls`, and one
  `wpt-css-sizing` layout case. Causes and ownership are not yet established;
  the complete results are in `temp/edit-viewer/markdown-final-radiant-baseline.log`.
- 2026-11-14 baseline follow-up, Lambda `09a40dfe2` and the explicitly
  authorized Radiant repair at `4ec1e8c` plus RI-089 changes. The two Lambda
  fixture fixes are committed in `09a40dfe2`: the paint fixture uses an ordinary
  parent for local rounded clipping (RI-087), retaining fallback counters and
  byte-identical fresh-render comparisons. Prefetch publishes matching consumers
  with stylesheet provenance (RI-079), loads five consumed resources, excludes
  an inactive rule and preserves all authored URLs. Both complete executables
  pass 250/250 and 186/186.
  [Radiant RI-089](../Radiant_Issue_Ledger.md#ri-089) is now
  fixed: intrinsic min/max measurement retains the definite preferred width
  before applying measured limits. `width:auto` and the existing `fit-content`
  negotiation remain intact. The new native regression fails before and passes
  after; all 110 owner dynamic tests pass, and independent Chromium confirms
  nine min/max combinations plus both fit-content controls. Integer lint checks
  89 casts with none unmarked. The sizing baseline passes 776/776 and the full
  `make test-radiant-baseline` exits 0 with 8,170 passed, zero failed, 297 partial
  and 20 skipped. Native ownership remains D9.1.1v4 / D9.1.3v2; no external
  fixture, reference or scored baseline changed. Evidence and source hashes are
  under `temp/radiant-three-failures/`, including `ri089-radiant-baseline.log`,
  `sizing-final-results.json`, `browser-control-final.json` and
  `ri089-verification-identity.json`.
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
- Next: after the owning Input fixes for LI-056–LI-059 and LI-061–LI-062,
  requalify TOML, YAML, calendar, contact, RST, Textile and man editing with
  malformed-input and source-preservation tests before expanding admission.
  Man also needs editor-side header retention. Independently expand the rich
  RTF subset only with preservation evidence; `highlight` is now supported,
  while `caps` still needs a compatible oracle. Do not silently rewrite source
  or change sibling providers. Ledger IDs in older entries above predate
  reconciliation.
