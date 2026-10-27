# DOCX export fixtures

`python3 test/input/docx/export/generate.py` reproduces all four source files
and the synthetic PNG using only the Python standard library. The inputs cover
Unicode/XML escaping, run styles, alignment, numbering/restarts, merged and
sparse grids, tabs, page breaks, embedded pictures, safe links and diagnosed
losses. No external URL is fetched.

Run the paired export goldens and generate their archives from the repo root:

```sh
./test/test_lambda_gtest.exe --gtest_filter='*docx_export*'
```

The public writer's procedural test writes `temp/docx-export/report.docx`, its
byte-identical second copy, a loss-bearing document and a protected sentinel.
The Word procedural test writes `word.docx` and verifies formatted editable
reopen without losing its table. The UI replay in
`test/demo/word/tests/docx_export.json` checks Export, reopening, yellow warnings
and the independent editable save checkpoint (15 assertions).

For a second implementation's read of the actual saved files:

```sh
python3 -m venv temp/docx-export/venv
temp/docx-export/venv/bin/pip install python-docx==1.2.0
temp/docx-export/venv/bin/python test/input/docx/export/check_export.py
temp/docx-export/venv/bin/python test/input/docx/export/check_cli.py
```

`check_export.py` independently opens ZIP/XML, checks package relationships,
content types, property order, numbering/restarts, table grids, image references,
page settings, atomic failure preservation and deterministic output, then uses
python-docx to verify editable paragraphs, run formatting, table merges and
picture extents. This is targeted structural validation, not an exhaustive
OOXML XSD conformance or a Microsoft Word rendering test. Native visual
artifacts and complete qualification results are recorded in the implementation
ledger under `vibe/impl/Lambda_Impl_Docx_Export.md`.
