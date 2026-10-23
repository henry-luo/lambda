# Word demo: equal-width columns

**Date:** 2026-10-23
**Authority:** **S1.4**, **S12.1.3**, **D7.2.5** for document state and
projection; **D2.10** for XML HTML input; **D4.5.1v4** for export editions.

## Delivered scope

Layout → Columns offers One, Two, Three and More Columns. The dialog sets an
equal-width count from 1 to 6, spacing in millimetres and an optional separator.
Settings apply to the whole document, enter history as one transaction, retain
the source selection, and survive editable and HTML round trips. Page setup and
horizontal ruler gestures reserve at least 20 mm per column.

`mod_columns.ls` owns validation and `data-word-columns`. `mod_surface.ls`
projects one column container per explicit page, applies each source child once,
and leaves repeated headers/footers outside the column flow. Thumbnails reuse
that projection without applying source bindings. One-column documents retain
the existing child structure. Read Mode and Web Layout retain column settings.
Paper CSS comes from `mod_page.print_style` for both ordinary and column exports.

## Export prerequisite and root causes

HTML parsing assigns HTML namespaces to prefixed custom tags. Native page
masters and flow controls therefore require XML input. The existing
`radiant.export_document` API now accepts `source_format:"xhtml"`, routed through
the shared XML HTML loader, export session and page composer. Omitted/`"html"`
keeps the existing parser. Unsupported values and malformed XHTML return the
existing diagnostic result; resource and ownership policy is unchanged.

Word exports ordinary HTML with column CSS and root metadata. For PDF it creates
native body-column controls, converts its generated document to well-formed
XHTML, and uses the explicit syntax option. The HTML-compatible XML formatter
retains raw-text head tags, so this adapter escapes the generated title/CSS for
XML parsing. Native body regions advance through columns and sheets without
splitting the source paragraphs. An HTML body margin can occupy a leading
implicit sheet before a named flow; column export resets that margin because
the page master already owns the inset.

## Acceptance and verification

The model golden covers count/spacing validation, narrow paper, ruler limits,
selection, editing, history, page-break typing, stories and editable/HTML/XML
round trips in interpreter, auto and JIT modes. The native replay covers the
menu/dialog, actual text fragments in both columns, typing, history, page breaks,
thumbnails, responsive views, table-cell editing, and file round trips.

The export fixture checks default/explicit HTML parity, XHTML diagnostics and
100 paragraphs in a five-sheet PDF. Independent PDF extraction checks every
paragraph appears once, column x positions, repeated headers, and page fields.
Neighboring layout, ruler, view, page-break, ribbon and story workflows cover
the shared presentation changes. Final evidence is under
`temp/word/tests/columns-results.json`; the required Radiant baseline has zero
failures and the Radiant integer-cast lint passes.

## Remaining scope

Unequal Left/Right presets, per-section settings and explicit column breaks are
not implemented. The editor retains its existing explicit-page presentation;
PDF performs automatic sheet pagination. Browser multicol fragmentation and
native paged composition retain their respective balancing behavior.
