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

Formulas support numbers (including scientific notation), quoted text, `TRUE` and
`FALSE`, cell references, absolute-reference spelling (`$A$1`), rectangular
ranges, and references to other worksheets such as `Sales!A1` and
`'Annual Plan'!A1:B4`. Parentheses, unary signs, `+`, `-`, `*`, `/`, `^`,
postfix `%`, text concatenation `&`, and comparisons `=`, `<>`, `<`, `<=`, `>`
and `>=` are supported. Operators of equal precedence evaluate left to right,
as in [Excel](https://support.microsoft.com/en-us/excel/calculation-operators-and-precedence-in-excel).
Function names are case-insensitive. The supported functions are:

| Category | Functions |
|---|---|
| Aggregation and criteria | `SUM`, `PRODUCT`, `AVERAGE`, `MIN`, `MAX`, `COUNT`, `COUNTA`, `COUNTBLANK`, `MEDIAN`, `SUMPRODUCT`, `SUMSQ`, `COUNTIF`, `COUNTIFS`, `SUMIF`, `SUMIFS`, `AVERAGEIF`, `AVERAGEIFS`, `MINIFS`, `MAXIFS`, `SUBTOTAL` |
| Math | `ABS`, `INT`, `SIGN`, `MOD`, `POWER`, `SQRT`, `ROUND`, `ROUNDUP`, `ROUNDDOWN`, `TRUNC`, `PI`, `EXP`, `LN`, `LOG`, `LOG10`, `SIN`, `COS`, `TAN`, `ASIN`, `ACOS`, `ATAN`, `ATAN2`, `DEGREES`, `RADIANS`, `QUOTIENT`, `EVEN`, `ODD`, `MROUND`, `FACT`, `COMBIN`, `CEILING.MATH`, `FLOOR.MATH`, `CEILING.PRECISE`, `FLOOR.PRECISE`, `SQRTPI`, `SINH`, `COSH`, `TANH`, `GCD`, `LCM` |
| Statistics | `STDEV`, `STDEV.S`, `STDEVP`, `STDEV.P`, `VAR`, `VAR.S`, `VARP`, `VAR.P`, `LARGE`, `SMALL`, `PERCENTILE`, `PERCENTILE.INC`, `QUARTILE`, `QUARTILE.INC`, `RANK`, `RANK.EQ`, `RANK.AVG`, `PERCENTILE.EXC`, `QUARTILE.EXC`, `SLOPE`, `INTERCEPT`, `FORECAST`, `FORECAST.LINEAR`, `CORREL`, `PEARSON`, `RSQ`, `MODE`, `MODE.SNGL`, `COVAR`, `COVARIANCE.P`, `COVARIANCE.S`, `STEYX`, `GEOMEAN`, `HARMEAN` |
| Finance | `PMT`, `PV`, `FV`, `NPER`, `NPV`, `IRR`, `XNPV`, `XIRR`, `RATE`, `SLN`, `SYD` |
| Text | `LEN`, `UPPER`, `LOWER`, `TRIM`, `LEFT`, `RIGHT`, `MID`, `CONCAT`, `CONCATENATE`, `TEXTJOIN`, `EXACT`, `VALUE`, `REPT`, `CLEAN`, `REPLACE`, `SUBSTITUTE`, `FIND`, `SEARCH`, `PROPER`, `TEXT`, `TEXTBEFORE`, `TEXTAFTER` |
| Dates and times | `DATE`, `DATEVALUE`, `YEAR`, `MONTH`, `DAY`, `WEEKDAY`, `EDATE`, `EOMONTH`, `DAYS`, `TIME`, `TIMEVALUE`, `HOUR`, `MINUTE`, `SECOND`, `NETWORKDAYS`, `NETWORKDAYS.INTL`, `WORKDAY`, `WORKDAY.INTL`, `DATEDIF`, `YEARFRAC`, `WEEKNUM`, `ISOWEEKNUM` |
| Logic and errors | `IF`, `IFS`, `CHOOSE`, `SWITCH`, `IFERROR`, `IFNA`, `AND`, `OR`, `NOT`, `TRUE`, `FALSE`, `NA`, `ISNUMBER`, `ISTEXT`, `ISBLANK`, `ISERROR`, `ISNA`, `LET` |
| Lookup | `ROWS`, `COLUMNS`, `INDEX`, `MATCH`, `VLOOKUP`, `HLOOKUP`, `XLOOKUP`, `XMATCH`, `LOOKUP`, `ROW`, `COLUMN`, `ADDRESS`, `INDIRECT`, `OFFSET` |

For example, `=SUMIFS(Sales!B1:B20,Sales!A1:A20,"East")` and
`=XLOOKUP(A1,'Annual Plan'!A1:A10,'Annual Plan'!B1:B10,"missing")` combine
functions and references. Formula parsing builds a graph of callable Lambda
closures. Each operator and Excel function resolves to a pure Lambda function
and is applied with `call(f, args)` (**S12.3.4v2**, **D7.2.2**); formula text
is never executed as Lambda source. Conditional functions evaluate only the
needed result branch.

This is a bounded Excel subset. Approximate and binary lookups require sorted
input; `MATCH`, `XMATCH`, and `XLOOKUP` accept one-dimensional ranges, and
`INDEX` and `XLOOKUP` return single cells. Criteria support the Excel `*`, `?`,
and `~` wildcard characters. Multiple criteria ranges must match in shape.
`DATEVALUE` accepts ISO `YYYY-MM-DD`, and `TIMEVALUE` accepts 24-hour `HH:MM`
or `HH:MM:SS`; imported 1900 and 1904 date systems are preserved. Workday
functions accept holiday ranges and Excel weekend numbers or Monday-first masks.
Paired statistics ignore nonnumeric pairs and return scalar results. Rounding
digits are bounded to -15 through 15. `TEXT` accepts the date masks `yyyy-mm-dd`,
`yyyy/mm/dd`, `mm/dd/yyyy`, `dd/mm/yyyy`, and numeric masks using `0` or `#,##0`, fixed
decimal places, an optional `$` prefix and an optional `%` suffix. `INDIRECT`
accepts A1-style references; `LOOKUP` supports its vector form. `LET` supports
up to seven name/value pairs. `SUBTOTAL` excludes directly nested subtotals and
ignores hidden rows for function numbers 101–111; filters are not implemented.
`IRR`, `XIRR` and `RATE` use bounded numerical iteration and can return `#NUM!`
when the calculation does not converge. External-workbook references, named and
structured references, full-column/row references, array/spill formulas,
custom formats, and the full Excel coercion and error rules are not supported.
`TODAY`, `NOW`, random functions, `AGGREGATE`, Excel `LAMBDA` and dynamic-array
functions such as `FILTER` and `UNIQUE` are not yet implemented. Invalid expressions,
missing references, division by zero, unknown functions and
circular dependencies produce cell errors.

Every edit recalculates the active sheet. A leading apostrophe forces a cell to
remain text.

Open accepts `.xlsx` and editable `.sheet.json` workbooks. XLSX imports include
multiple worksheets, values, formulas, common styles and dimensions, merged
ranges, hidden rows and columns, frozen row/column prefixes, and split panes.
Sheet displays merged cells across the visible grid and skips hidden dimensions
during arrow navigation; their data remains available to formulas. Frozen
prefixes remain visible while scrolling. Split panes have independent scroll
positions, with matching axes synchronized; the saved starting cell and active
pane are restored on open. Imported metadata survives `.sheet.json` save/restore.

Conditional formatting supports formula expressions, numeric `cellIs`
comparisons with constant or formula operands, case-insensitive plain-text
contains/starts/ends rules, blank and nonblank rules, and RGB font/fill
differential styles. Relative and mixed references are adjusted from the first
applied range; rules use Excel priority and `stopIfTrue`, and update when cells
recalculate. Formulas use Sheet's bounded function and reference profile
(**S12.3.4v2**, **D7.2.2**); an error or unsupported expression does not apply
its rule. Colour scales, data bars, icon sets, theme colours and other
differential styles remain unsupported and produce an import warning. Rules
outside Sheet's grid are clipped with a warning. Shared and array formulas,
charts, drawings, worksheet/workbook protection, and complete Excel
number-format and formula fidelity remain outside this profile. Imported split
dividers can be dragged or moved with the arrow keys and are saved in
`.sheet.json`; creating or removing splits is not yet supported. Current
scrolling is transient and opening the workbook restores its saved starting
positions.

Built-in and common custom date/time formats display as ISO dates and 24-hour
times using the workbook’s 1900 or 1904 calendar. Open reports import warnings
in the status bar. Unsupported formulas show their saved XLSX result when
available; an edit invalidates imported formula results.

Save writes an editable `.sheet.json` workbook, preserving the supported formulas,
cell styles, row heights, column widths, imported layout and worksheets. XLSX
import does not enable XLSX export, and the save dialog does not overwrite an `.xlsx` file with JSON.
Writes are atomic. Export CSV writes the active sheet’s calculated values with
quoted fields. New, Open and Studio example offer
Save/Discard/Cancel when the workbook has unsaved changes.

This first milestone covers an editable desktop spreadsheet. CSV import,
formula reference translation during copy/sort, charts, filters, creating or
changing merged ranges and drag fill remain future work. Clipboard and sorting retain formula source text.
The default grid is 60 × 20; loaded workbooks support at most 100,000 rows,
1,000 columns and 50 worksheets. The grid generates only visible rows and
columns plus a small overscan; selection and navigation use logical cell
addresses across the whole sheet. Dragging at an edge scrolls the viewport.
Copy, formatting, sorting and CSV export are limited to a rectangle of one
million cells while those operations materialize their results. Formula source
is limited to 512 characters, function calls to 16 arguments (**S12.3.1**),
and nested evaluation/dependencies to 64 levels.

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
./test/test_lambda_gtest.exe --gtest_filter='*sheet_formula_common_new_test*:*sheet_formula_test*:*sheet_formula_extended_test*:*sheet_formula_more_test*:*sheet_xlsx_external_test*:*sheet_xlsx_layout_test*:*sheet_conditional_formula_test*:*sheet_split_panes_test*:*sheet_model_test*:*sheet_io_test*:*sheet_resize_test*'

# Native UI workflows, discovered by the baseline UI manifest.
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_editor.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_formula_functions.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_ribbon.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_selection.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_handle.json
./lambda.exe demo sheet --headless --event-file test/ui/sheet_files.json
./lambda.exe demo sheet --headless --event-file test/ui/sheet_xlsx.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_xlsx_external.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_xlsx_layout.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_conditional_formula.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_split_panes.json
./lambda.exe view test/demo/sheet/sheet.ls --headless \
  --event-file test/ui/sheet_split_resize.json
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
