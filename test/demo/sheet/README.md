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
accepts a cell or range such as `C5` or `C5:E10`. Drag across cells to select a
rectangle; Shift-click and Shift-arrow extend a selection. Escape during a drag
restores the previous selection. Row and column headers select a whole row or column.
Drag the small rectangle at the bottom-right corner to grow or shrink the selected range.
The pointer becomes “+” over the corner and stays “+” throughout the drag.

Drag a column header's right edge to change its width, or a row header's bottom
edge to change its height. Sizes preview during the drag; releasing creates one
undoable change, and Escape cancels it. Focus an edge and use the arrow keys for
8-pixel adjustments (Shift for 1 pixel), or Home/End for the minimum/maximum.
Dimensions are stored before zoom: columns support 40–1000 pixels and rows
27–500 pixels. Manually sized rows clip wrapped text to the chosen height.

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

Open accepts `.xlsx` and editable `.sheet.json` workbooks. XLSX imports include multiple
worksheets, values, formula source, common cell styles and dimensions. Built-in
Excel date formats 14–17 display as ISO dates using the workbook’s 1900 or 1904
calendar. Open reports import warnings in the status bar. Unsupported formulas
show their saved XLSX result when available; any edit invalidates all imported
formula results, after which Sheet shows its calculation errors for formulas it
cannot evaluate. Shared and array formulas, merged cells, drawings, many styles,
custom date/time formats and Excel formula semantics are not fully supported.

Save writes an editable `.sheet.json` workbook, preserving the supported formulas,
cell styles, row heights, column widths and worksheets. XLSX import does not enable
XLSX export, and the save dialog does not overwrite an `.xlsx` file with JSON.
Writes are atomic. Export CSV writes the active sheet’s calculated values with
quoted fields. New, Open and Studio example offer
Save/Discard/Cancel when the workbook has unsaved changes.

This first milestone covers an editable desktop spreadsheet. CSV import,
cross-sheet references, formula reference
translation during copy/sort, charts, filters, merged cells and drag fill remain
future work. Clipboard and sorting retain formula source text.
The default grid is 60 × 20; loaded workbooks support at most 100,000 rows,
1,000 columns and 50 worksheets. The grid generates only visible rows and
columns plus a small overscan; selection and navigation use logical cell
addresses across the whole sheet. Dragging at an edge scrolls the viewport.
Copy, formatting, sorting and CSV export are limited to a rectangle of one
million cells while those operations materialize their results. Formula source
is limited to 512 characters and nested evaluation/dependencies to 64 levels.

The shell reuses Word's theme, header, buttons and status styles from
`lambda.ui.workspace.office`. Word loads those same styles. Ribbon layout and controls use
`lambda.ui.workspace.ribbon_style`, shared `lambda.ui.workspace.ribbon` and
`lambda.ui.workspace.ribbon_icons`, `lambda.ui.workspace.text_format` palettes and
`lambda.ui.workspace.ribbon_overflow` resizing. Pointer capture and separator
keyboard sizing reuse `lambda.ui.core.splitter`.
Slides also loads the extracted ribbon stylesheet. The app uses the shared entry
resource resolver and demo catalog/preview infrastructure. Spreadsheet state and
commands stay in Lambda, with pure rendering and handler-owned effects
(**S12.1.3**, **D10.2.1**); procedure dispatch captures the successful result in its
value arm (**S7.6.7v4**).

Verification:

```sh
# Model and workbook/format goldens, registered in the Lambda test harness.
./test/test_lambda_gtest.exe --gtest_filter='*sheet_model_test*:*sheet_io_test*:*sheet_resize_test*'

# Native UI workflows, discovered by the baseline UI manifest.
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_editor.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_ribbon.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_selection.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_handle.json
./lambda.exe demo sheet --headless --event-file test/ui/sheet_files.json
./lambda.exe demo sheet --headless --event-file test/ui/sheet_xlsx.json
./lambda.exe demo sheet --headless --event-file test/ui/sheet_resize.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_virtual_grid.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_virtual_drag.json

# Refresh the demo index's actual app screenshot.
sh test/demo/previews/capture.sh sheet
```

The UI checks cover cell editing, recalculation, pointer ranges and cancellation,
row/column sizing with zoom, keyboard adjustments and wrapped text, formatting,
clipboard, undo/redo, formula display, worksheet creation, editable save/open, CSV export,
unsaved-change prompts and renaming. The Word ribbon appearance and Slides Home
action replays cover the shared stylesheet extraction.
