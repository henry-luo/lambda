"""Open each redistributable workbook and check a representative visible cell."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / 'temp/xlsx-external-ui'
CASES = [
    ('bloated-workbook.xlsx', 1, 'A1', 'Order ID', 0),
    ('contacts.xlsx', 1, 'A1', 'First Name', 0),
    ('customer-lists.xlsx', 1, 'A1', 'CRM Customers', 0),
    ('employees.xlsx', 3, 'A1', 'Employee ID', 0),
    ('event-schedule.xlsx', 1, 'B2', '2026-11-02', 0),
    ('exceljs-1904.xlsx', 3, 'B4', '1904-01-01', 0),
    ('excelize-Book1.xlsx', 2, 'A1', 'Monitor', 1),
    ('excelize-MergeCell.xlsx', 1, 'A1', 'A1', 0),
    ('excelize-SharedStrings.xlsx', 2, 'A1', 'A', 0),
    ('files-sample-1000rows.xlsx', 1, 'A1', 'ID', 0),
    ('formatted-report.xlsx', 1, 'A1', 'Q3 2025 Sales by Region', 0),
    ('hidden-frozen.xlsx', 1, 'A1', 'Cost centre', 0),
    ('inventory.xlsx', 2, 'A1', 'SKU', 0),
    ('merged-formats.xlsx', 1, 'A1', 'Q3 2026 Regional Sales Report', 0),
    ('messy-data.xlsx', 1, 'A1', 'Name', 0),
    ('numbers-stored-as-text.xlsx', 1, 'A1', 'Invoice #', 0),
    ('protected-sample.xlsx', 2, 'A1', 'Category', 0),
    ('sales-orders.xlsx', 2, 'A1', 'Order ID', 0),
    ('sample-budget.xlsx', 2, 'A1', 'Category', 0),
    ('workbook-protected.xlsx', 2, 'A1', 'Category', 0),
]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    failures = []
    for name, sheets, cell, expected, tab in CASES:
        events = [
            {'type': 'wait', 'ms': 50},
            {'type': 'click', 'target': {'selector': '#open'}},
            {'type': 'type', 'target': {'selector': '#file-path'},
             'text': 'test/input/xlsx/external/' + name, 'clear': True},
            {'type': 'click', 'target': {'selector': '#dialog-confirm'}},
            {'type': 'assert_attribute', 'target': {'selector': '#sheet'},
             'attribute': 'data-sheets', 'equals': str(sheets)},
        ]
        if tab:
            events.append({'type': 'click', 'target': {'selector': '#sheet-tab-' + str(tab)}})
        events.append({'type': 'assert_text', 'target': {'selector': '#cell-' + cell}, 'contains': expected})
        path = OUT / 'events.json'
        path.write_text(json.dumps({'name': name, 'html': 'test/demo/sheet/sheet.ls',
                                    'viewport': {'width': 1440, 'height': 900},
                                    'input_turn_ms': 0, 'events': events}))
        try:
            proc = subprocess.run(['./lambda.exe', 'view', 'test/demo/sheet/sheet.ls',
                                   '--headless', '--event-file', str(path.relative_to(ROOT))],
                                  cwd=ROOT, capture_output=True, text=True, timeout=90)
            output = proc.stdout + proc.stderr
            (OUT / (name + '.log')).write_text(output)
            passed = proc.returncode == 0 and 'Assertions: 2 passed, 0 failed' in output
        except subprocess.TimeoutExpired:
            passed = False
        print(name + (': PASS' if passed else ': FAIL'), flush=True)
        if not passed:
            failures.append(name)
    print(f'{len(CASES) - len(failures)}/{len(CASES)} workbooks passed')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
