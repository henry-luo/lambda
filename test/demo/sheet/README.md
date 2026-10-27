# Sheet

An Excel-style spreadsheet demo written in Lambda and rendered by Radiant.

```sh
./lambda.exe demo sheet
```

The initial workbook contains a quarterly operating budget with live totals,
budget variance and utilization, plus Forecast and Notes worksheets. New starts
an empty workbook; Studio example restores the sample.

Click a cell to select it. Double-click or press F2 to edit, or type directly to
replace its contents. Enter commits and moves down; Tab moves across. Escape
cancels the draft. The formula bar edits the active cell, and the address box
accepts a cell or range such as `C5` or `C5:E10`. Shift-click and Shift-arrow
extend a selection; row and column headers select a whole row or column.

Home provides undo/redo, plain-text clipboard copy/cut/paste, font and fill colors,
type size, emphasis, alignment, wrapping, borders, number/currency/percent formats,
AutoSum, and sorting of the selected rectangle by its active column. The status
bar shows the selected range's count, sum and average. Insert adds or renames
worksheets and extends the grid by ten rows. View toggles gridlines, displays
formulas and changes zoom. Ctrl/Cmd-S opens Save; Ctrl/Cmd-Z/Y control history;
Ctrl/Cmd-C/X/V and Ctrl/Cmd-B/I/U work on the grid.

Formulas support numeric literals, cell references, absolute-reference spelling
(`$A$1`), rectangular ranges, parentheses, unary signs, `+`, `-`, `*`, `/`, `^`,
postfix `%`, and `SUM`, `AVERAGE`, `MIN`, `MAX`, `COUNT`. Examples:

```text
=SUM(C5:E5)
=G5-F5
=AVERAGE(C11:E11)
=SUM(A1:B4, 10) / 2
```

Every edit recalculates from the workbook. Invalid expressions, missing references,
division by zero, unknown functions and circular dependencies produce cell errors.
Formula source is parsed as spreadsheet expressions; it is never executed as
Lambda code. A leading apostrophe forces a cell to remain text.

Save and Open use editable `.sheet.json` workbooks, preserving formulas, cell
styles and every worksheet. Writes are atomic. Export CSV writes the active
sheet's calculated values with quoted fields. New, Open and Studio example offer
Save/Discard/Cancel when the workbook has unsaved changes.

This first milestone covers an editable desktop spreadsheet. XLSX/CSV import,
cross-sheet references, formula reference
translation during copy/sort, charts, filters, merged cells, drag fill and column
resizing remain future work. Clipboard and sorting retain formula source text.
The default grid is 60 × 20; loaded workbooks support at most 500 rows, 100 columns
and 50 worksheets. Formula source is limited to 512 characters and nested
evaluation/dependencies to 64 levels. The grid is rendered in full.

The shell reuses Word's theme, header, buttons and status styles from
`../office.css`. Word loads that same stylesheet. Ribbon layout and controls use
Slides' extracted `../ribbon.css`, the shared `../mod_ribbon.ls`, Word's ribbon
icons, `../mod_text_format.ls` palettes and `../mod_ribbon_overflow.ls` resizing.
Slides also loads the extracted ribbon stylesheet. The app uses the shared entry
resource resolver and demo catalog/preview infrastructure. Spreadsheet state and
commands stay in Lambda, with pure rendering and handler-owned effects
(**S12.1.3**, **D7.2.5**); procedure dispatch captures the successful result in its
value arm (**S7.6.7v4**).

Verification:

```sh
# Model and workbook/format goldens, registered in the Lambda test harness.
./test/test_lambda_gtest.exe --gtest_filter='*sheet_model_test*:*sheet_io_test*'

# Native UI workflows, discovered by the baseline UI manifest.
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_editor.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_ribbon.json
./lambda.exe demo sheet --headless --event-file test/ui/sheet_files.json

# Refresh the demo index's actual app screenshot.
sh test/demo/previews/capture.sh sheet
```

The UI checks cover cell editing, recalculation, ranges, formatting, clipboard,
undo/redo, formula display, worksheet creation, editable save/open, CSV export,
unsaved-change prompts and renaming. The Word ribbon appearance and Slides Home
action replays cover the shared stylesheet extraction.
