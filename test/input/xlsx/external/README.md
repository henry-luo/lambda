# External XLSX corpus

These 20 unmodified downloads cover independent workbook features and producers.
The exact URLs, SHA-256 hashes, licenses and feature descriptions are in
[`manifest.json`](manifest.json). Publisher checksums were checked for the
ExcelTool files; the remaining hashes identify the downloaded bytes.

- [ExcelTool fixtures and CC0 license](https://www.exceltool.io/sample-excel-files);
  its [published manifest](https://www.exceltool.io/fixtures/manifest.json) supplies checksums.
- [Files Sample XLSX license](https://files-sample.com/sample/documents/xlsx/): CC0-1.0.
- [ExcelJS](https://github.com/exceljs/exceljs/tree/5bed18b45e824f409b08456b59b87430ded023ab/spec/integration/data):
  MIT; the required notice is in [LICENSE.exceljs](LICENSE.exceljs).
- [Excelize](https://github.com/qax-os/excelize/tree/e7deced25afc947ecf801690cb51fe47550ab306/test):
  BSD-3-Clause; the required notice is in [LICENSE.excelize](LICENSE.excelize).

| Workbook | Coverage |
|---|---|
| `bloated-workbook.xlsx` | 120 real orders with fill and borders applied to 6,000 empty rows below them across columns A:N (the classic 'formatted whole columns' bloat), plus three cell notes. The used range reaches row 6,121. |
| `contacts.xlsx` | 24 invented business contacts with first/last name, company, title, email, mobile and work phones (555-01xx numbers reserved for fiction), full street address, website and free-text notes that contain extra emails and phone numbers. |
| `customer-lists.xlsx` | Two customer lists side by side (30 in the CRM, 28 in billing) that mostly overlap. One billing name has a trailing space and one differs only in capitalisation. |
| `employees.xlsx` | Practice dataset: 60 invented employees (ID, name, email, department, title, office, hire date, salary, status) and a Departments sheet with a COUNTIF headcount. Carries the metadata a real HR file leaks: author, last modified by, company, manager, three custom properties and the saved folder path. |
| `event-schedule.xlsx` | 12 company calendar events from November 2026 to January 2027: timed meetings, an overnight offsite and all-day US holidays, with dates and times stored as real Excel date/time values. |
| `exceljs-1904.xlsx` | Three worksheets with the 1904 workbook date system and built-in date formats. |
| `excelize-Book1.xlsx` | Two sheets, rich text, formulas, charts, table and hyperlinks |
| `excelize-MergeCell.xlsx` | Multiple horizontal and vertical merged ranges |
| `excelize-SharedStrings.xlsx` | Plain and rich shared-string runs on multiple sheets |
| `files-sample-1000rows.xlsx` | 1,000-row inline-string table from another producer |
| `formatted-report.xlsx` | A styled Q3 report: merged title, custom fonts, filled header row, borders, a highlighted total column, red negative variances, a double-underlined total row and a frozen header. |
| `hidden-frozen.xlsx` | Two hidden columns and one hidden row, a frozen header row and frozen label column, custom row heights, and currency formats. The hidden columns are why the visible quarters do not add up to the printed total - the case that separates a viewer reporting what Excel stores from one quietly dropping it. |
| `inventory.xlsx` | Practice dataset: 40 invented products - SKU, Product, Category, Supplier, Warehouse, Qty On Hand, Reorder Point, Unit Cost, Unit Price, Last Restocked. About one in five items is below its reorder point. |
| `merged-formats.xlsx` | Merged title/header ranges, a frozen top row, currency/percent/date number formats, and custom column widths - exercises exactly the metadata a data-view renderer keeps or drops. |
| `messy-data.xlsx` | A contact list with exact duplicates, case-variant duplicates, stray whitespace, inconsistent casing, and blank rows - the classic cleanup workload. |
| `numbers-stored-as-text.xlsx` | 15 invoices where every amount, quantity, discount and balance is stored as text ("$1,250.00", " 12", "15%", "(120.50)"), so the SUM totals come out as 0. ZIP codes with leading zeros are text on purpose. |
| `protected-sample.xlsx` | Worksheet protection (&lt;sheetProtection&gt; with the legacy 16-bit password hash) on both sheets. The cells are locked against editing but the data is fully readable. |
| `sales-orders.xlsx` | Practice dataset: 250 invented office-supply orders from 2025 across four US regions - Order ID, Order Date, Region, State, Sales Rep, Segment, Category, Product, Units, Unit Price and a live Revenue formula. Frozen header, AutoFilter, USD and M/D/YYYY formats, plus an About sheet. |
| `sample-budget.xlsx` | Two worksheets, SUM/variance formulas with cached values, typed columns. General-purpose sample for viewers, converters, and the compare tool. |
| `workbook-protected.xlsx` | Workbook structure protection (&lt;workbookProtection lockStructure="1"&gt;). Sheets cannot be added, deleted, renamed, moved, or unhidden in Excel until it is removed. |

## Survey results

The XLSX package and Sheet conversion opened all 20 workbooks. A headless Sheet
open checked a representative visible cell in each workbook, switching sheets
for `excelize-Book1.xlsx`. These checks do not prove complete visual fidelity.
The large formatted workbook also opens after replacing quadratic duplicate-cell
validation with keyed checks.

An excluded Excelize calculation-chain candidate had a case-mismatched internal
relationship. A generated malformed fixture checks that the package rejects
such broken OPC references, whose part names are case-sensitive.

Sheet normalizes common date and time formats and displays imported merges,
hidden dimensions and frozen prefixes. Conditional formatting covers numeric
cell comparisons with constant operands, text and blank rules, and RGB
font/fill styles. Charts,
drawings, protection, split panes and other conditional rule types remain
unsupported; the importer reports these selected gaps.
Other unsupported features may lack diagnostics. Formula results use the
supported Sheet evaluator or a saved cached value where applicable.

The focused integration test is `test/demo/sheet/tests/sheet_xlsx_external_test.ls`;
`test/ui/sheet_xlsx_external.json` checks date/time display through Sheet Open.
Run `python3 test/demo/sheet/tests/verify_external_ui.py` to repeat all 20 UI
opens; it writes event files and logs under `temp/xlsx-external-ui/`.
