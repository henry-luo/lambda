# DOCX package-preserving editing

## Checkpoint — 2026-11-16

- Objective: repair Input LI-063 and add graphical DOCX editing that retains
  unknown package parts (D9.3.4, D10.2.1, D10.2.5, S12.1.1v2).
- Lambda HEAD `ffd7d5031`; Input HEAD `2672c1a`. Input and Radiant contain
  earlier authorized editor changes; Lambda has unrelated Midtown work. Do
  not overwrite those changes. The user authorized the Input repair.
- LI-063: Input now accepts validated ZIP timestamps and modes for constructed
  entries and distinguishes synthetic directories from original members.
  Package reconstruction carries the metadata through. Focused ZIP suite:
  37/37, including file and directory metadata, raw records, ZIP64, data
  descriptors, member order and archive comments.
- DOCX: the editor captures the original package and main XML source, and
  maps supported paragraphs and simple one-paragraph table cells into the rich
  surface. It uses the DOCX writer for new runs and patches those runs into
  the source, retaining the remaining main XML and all other package members.
  Unsupported content is read-only, structural table edits are refused, and
  signature-origin packages are read-only. Binary sessions support save, Save
  As, undo, reload and external-change detection; the source editor is not
  offered for binary packages.
- Preservation: unchanged original local records and central metadata, member
  order, archive comments and implicit-directory status are retained. An
  independent ZIP inspection of the opaque-member fixture and a 12-member
  reference DOCX passed (`temp/docx-edit/package-oracle.log`). A no-op and an
  undo to the saved snapshot retain the original package bytes.
- Focused tests: eight Lambda tests pass in both default and interpreter mode;
  the complete editor UI sweep passes 222/222. Input's aggregate baseline
  completed with three existing failures: two LI-003 copied-contract tests and
  the LI-002 transition-tree admission test (`temp/docx-edit/input-baseline.log`).
  Lambda baseline passes 7,554/7,554 (`temp/docx-edit/lambda-baseline.log`).
  Radiant baseline passes with 8,171 passed, 297 partial and 20 skipped
  (`temp/docx-edit/radiant-baseline.log`). Test262 reports 40,259 fully
  passing, two non-fully-passing cases recovered on isolated retry, zero
  failures or reported regressions, and 2,652 skipped
  (`temp/docx-edit/test262-baseline.log`).
- Boundaries: complex runs, nested or multi-paragraph cells, merged cells,
  table structural changes and signed packages are not editable. Rejected or
  corrupt binary packages cannot fall back to a source editor. EPUB work was
  deferred by the user and is outside this DOCX task.
