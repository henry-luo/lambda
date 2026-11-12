"""Generate a small SpreadsheetML interoperability fixture using only stdlib."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo

HERE = Path(__file__).resolve().parent
X = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
R = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
P = 'http://schemas.openxmlformats.org/package/2006/relationships'
CT = 'http://schemas.openxmlformats.org/package/2006/content-types'
files = {
    '[Content_Types].xml': f'''<Types xmlns="{CT}"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/></Types>''',
    '_rels/.rels': f'''<Relationships xmlns="{P}"><Relationship Id="main" Type="{R}/officeDocument" Target="xl/workbook.xml"/></Relationships>''',
    'xl/workbook.xml': f'''<workbook xmlns="{X}" xmlns:r="{R}"><sheets><sheet name="Sales" sheetId="1" r:id="s1"/><sheet name="Notes" sheetId="2" r:id="s2"/></sheets></workbook>''',
    'xl/_rels/workbook.xml.rels': f'''<Relationships xmlns="{P}"><Relationship Id="s1" Type="{R}/worksheet" Target="worksheets/sales.xml"/><Relationship Id="s2" Type="{R}/worksheet" Target="worksheets/notes.xml"/><Relationship Id="strings" Type="{R}/sharedStrings" Target="sharedStrings.xml"/><Relationship Id="styles" Type="{R}/styles" Target="styles.xml"/></Relationships>''',
    'xl/sharedStrings.xml': f'''<sst xmlns="{X}"><si><t>Product</t></si><si><r><t>Shared </t></r><r><t>text</t></r></si><si><t>00123</t></si></sst>''',
    'xl/styles.xml': f'''<styleSheet xmlns="{X}"><fonts count="2"><font><name val="Arial"/><sz val="11"/></font><font><b/><color rgb="FFFF0000"/><sz val="14"/></font></fonts><fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FF00FF00"/></patternFill></fill></fills><cellXfs count="3"><xf fontId="0" fillId="0" numFmtId="0"/><xf fontId="1" fillId="1" numFmtId="10"><alignment horizontal="center" wrapText="1"/></xf><xf fontId="0" fillId="0" numFmtId="14"/></cellXfs></styleSheet>''',
    'xl/worksheets/sales.xml': f'''<worksheet xmlns="{X}"><cols><col min="1" max="2" width="20" customWidth="1"/></cols><sheetData><row r="1" ht="30" customHeight="1"><c r="A1" t="s" s="1"><v>0</v></c><c r="B1" t="inlineStr"><is><t>Price</t></is></c><c r="C1" t="s"><v>2</v></c></row><row r="2"><c r="A2" t="s"><v>1</v></c><c r="B2"><v>12.5</v></c><c r="C2" t="b"><v>1</v></c><c r="D2"><f>SUM(B2,2)</f><v>14.5</v></c><c r="E2"><f>FORECAST.ETS(B2,A1:A2,B1:B2)</f><v>42</v></c><c r="F2" s="2"><v>45292</v></c><c r="G2"><f>_xlfn.XLOOKUP(B2,B2:B2,D2:D2)</f><v>14.5</v></c><c r="H2"><f>YEAR(F2)</f><v>2024</v></c><c r="I2"><f>FORECAST.LINEAR(20,B2:D2,B2:D2)</f><v>99</v></c></row><row r="3"><c r="H3" t="str"><f>Notes!A1</f><v>Second worksheet</v></c></row></sheetData></worksheet>''',
    'xl/worksheets/notes.xml': f'''<worksheet xmlns="{X}"><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>Second worksheet</t></is></c></row></sheetData><mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells></worksheet>''',
}
with ZipFile(HERE / 'basic.xlsx', 'w', ZIP_DEFLATED) as archive:
    for name, body in files.items():
        info = ZipInfo(name, (2020, 1, 1, 0, 0, 0))
        info.compress_type = ZIP_DEFLATED
        archive.writestr(info, body.encode())

# Mutations isolate failure paths and the workbook's alternate date system.
def variant(filename, member, replace_from, replace_to):
    altered = dict(files)
    altered[member] = altered[member].replace(replace_from, replace_to)
    with ZipFile(HERE / filename, 'w', ZIP_DEFLATED) as archive:
        for name, body in altered.items():
            info = ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            archive.writestr(info, body.encode())

variant('bad-shared-string.xlsx', 'xl/worksheets/sales.xml', '<v>0</v>', '<v>999</v>')
variant('bad-relationship.xlsx', 'xl/workbook.xml', 'r:id="s2"', 'r:id="missing"')
variant('date1904.xlsx', 'xl/workbook.xml', '<sheets>', '<workbookPr date1904="1"/><sheets>')
variant('bad-case-relationship.xlsx', 'xl/_rels/workbook.xml.rels', 'Target="sharedStrings.xml"', 'Target="SharedStrings.xml"')

# Format tokens inside literals and color brackets must not turn numbers into dates.
format_files = dict(files)
format_files['xl/styles.xml'] = f'''<styleSheet xmlns="{X}"><numFmts count="3"><numFmt numFmtId="164" formatCode="yyyy-mm-dd hh:mm:ss"/><numFmt numFmtId="165" formatCode="0 &quot;days&quot;"/><numFmt numFmtId="166" formatCode="mm/dd/yyyy"/></numFmts><cellXfs count="5"><xf numFmtId="0"/><xf numFmtId="164"/><xf numFmtId="165"/><xf numFmtId="18"/><xf numFmtId="166"/></cellXfs></styleSheet>'''
format_files['xl/worksheets/sales.xml'] = f'''<worksheet xmlns="{X}"><sheetData><row r="1"><c r="A1" s="1"><v>45292.5</v></c><c r="B1" s="2"><v>7</v></c><c r="C1" s="3"><v>0.375</v></c><c r="D1" s="4"><v>45292</v></c></row></sheetData></worksheet>'''
with ZipFile(HERE / 'number-formats.xlsx', 'w', ZIP_DEFLATED) as archive:
    for name, body in format_files.items():
        info = ZipInfo(name, (2020, 1, 1, 0, 0, 0))
        info.compress_type = ZIP_DEFLATED
        archive.writestr(info, body.encode())

# Worksheet layout and numeric conditional rules with two differential styles.
layout_files = dict(files)
layout_files['xl/styles.xml'] = f'''<styleSheet xmlns="{X}"><fonts count="1"><font><sz val="11"/></font></fonts><fills count="1"><fill><patternFill patternType="none"/></fill></fills><cellXfs count="1"><xf fontId="0" fillId="0" numFmtId="0"/></cellXfs><dxfs count="2"><dxf><font><color rgb="FFFF0000"/><b/></font><fill><patternFill patternType="solid"><fgColor rgb="FFFFFF00"/></patternFill></fill></dxf><dxf><font><i/></font></dxf></dxfs></styleSheet>'''
layout_files['xl/worksheets/sales.xml'] = f'''<worksheet xmlns="{X}"><sheetViews><sheetView workbookViewId="0"><pane xSplit="1" ySplit="1" topLeftCell="B2" state="frozen"/></sheetView></sheetViews><cols><col min="4" max="4" hidden="1" width="12"/></cols><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>Layout title</t></is></c></row><row r="2"><c r="A2" t="inlineStr"><is><t>Group</t></is></c><c r="B2"><v>15</v></c><c r="C2" t="inlineStr"><is><t>Warning</t></is></c></row><row r="3"><c r="B3"><v>5</v></c></row><row r="4"><c r="B4"><v>20</v></c></row><row r="5" hidden="1"><c r="B5"><v>99</v></c></row></sheetData><mergeCells count="2"><mergeCell ref="A1:C1"/><mergeCell ref="A2:A3"/></mergeCells><conditionalFormatting sqref="B2:B4"><cfRule type="cellIs" dxfId="0" priority="1" operator="greaterThan" stopIfTrue="1"><formula>10</formula></cfRule><cfRule type="cellIs" dxfId="1" priority="2" operator="greaterThan"><formula>0</formula></cfRule><cfRule type="colorScale" priority="3"/></conditionalFormatting><conditionalFormatting sqref="C2:C4"><cfRule type="containsText" text="warn" dxfId="1" priority="4"/><cfRule type="containsBlanks" dxfId="0" priority="5"/></conditionalFormatting></worksheet>'''
with ZipFile(HERE / 'layout.xlsx', 'w', ZIP_DEFLATED) as archive:
    for name, body in layout_files.items():
        info = ZipInfo(name, (2020, 1, 1, 0, 0, 0))
        info.compress_type = ZIP_DEFLATED
        archive.writestr(info, body.encode())

# Full-column formatting remains usable within the Sheet grid and reports clipping.
clipped_files = dict(layout_files)
clipped_files['xl/worksheets/sales.xml'] = clipped_files['xl/worksheets/sales.xml'].replace('sqref="B2:B4"', 'sqref="B2:B1048576"')
with ZipFile(HERE / 'layout-clipped.xlsx', 'w', ZIP_DEFLATED) as archive:
    for name, body in clipped_files.items():
        info = ZipInfo(name, (2020, 1, 1, 0, 0, 0))
        info.compress_type = ZIP_DEFLATED
        archive.writestr(info, body.encode())

# A merged range can begin in a hidden column; the visible fragment still owns its value.
hidden_anchor_files = dict(layout_files)
hidden_anchor_files['xl/worksheets/sales.xml'] = hidden_anchor_files['xl/worksheets/sales.xml'].replace(
    '<col min="4" max="4" hidden="1" width="12"/>', '<col min="1" max="1" hidden="1" width="12"/>')
with ZipFile(HERE / 'layout-hidden-anchor.xlsx', 'w', ZIP_DEFLATED) as archive:
    for name, body in hidden_anchor_files.items():
        info = ZipInfo(name, (2020, 1, 1, 0, 0, 0))
        info.compress_type = ZIP_DEFLATED
        archive.writestr(info, body.encode())

# Formula rules use one range anchor; relative and absolute components shift per cell.
formula_files = dict(layout_files)
formula_files['xl/worksheets/sales.xml'] = f'''<worksheet xmlns="{X}"><sheetData><row r="1"><c r="E1"><v>10</v></c></row><row r="2"><c r="B2"><v>15</v></c><c r="C2"><v>1</v></c><c r="D2"><v>15</v></c><c r="E2"><v>20</v></c></row><row r="3"><c r="B3"><v>5</v></c><c r="C3"><v>1</v></c><c r="D3"><v>5</v></c><c r="E3"><v>3</v></c></row><row r="4"><c r="B4"><v>20</v></c><c r="C4"><v>1</v></c><c r="D4"><v>20</v></c></row></sheetData><conditionalFormatting sqref="C2:C4"><cfRule type="expression" dxfId="0" priority="1"><formula>$B2&gt;$E$1</formula></cfRule></conditionalFormatting><conditionalFormatting sqref="D2:D4"><cfRule type="cellIs" dxfId="1" priority="2" operator="greaterThan"><formula>$E1</formula></cfRule></conditionalFormatting></worksheet>'''
with ZipFile(HERE / 'conditional-formulas.xlsx', 'w', ZIP_DEFLATED) as archive:
    for name, body in formula_files.items():
        info = ZipInfo(name, (2020, 1, 1, 0, 0, 0))
        info.compress_type = ZIP_DEFLATED
        archive.writestr(info, body.encode())

# Split pane coordinates are twips, and topLeftCell anchors the lower/right view.
split_files = dict(layout_files)
split_files['xl/worksheets/sales.xml'] = split_files['xl/worksheets/sales.xml'].replace(
    '<pane xSplit="1" ySplit="1" topLeftCell="B2" state="frozen"/>',
    '<pane xSplit="6000" ySplit="4500" topLeftCell="E7" activePane="bottomRight" state="split"/><selection pane="bottomRight" activeCell="F8" sqref="F8"/>')
with ZipFile(HERE / 'split-panes.xlsx', 'w', ZIP_DEFLATED) as archive:
    for name, body in split_files.items():
        info = ZipInfo(name, (2020, 1, 1, 0, 0, 0))
        info.compress_type = ZIP_DEFLATED
        archive.writestr(info, body.encode())

# Single-axis split variants exercise two panes and active-pane normalization.
for filename, pane in [
    ('split-columns.xlsx', '<pane xSplit="6000" topLeftCell="E7" activePane="topRight" state="split"/>'),
    ('split-rows.xlsx', '<pane ySplit="4500" topLeftCell="E7" activePane="bottomLeft" state="split"/>'),
]:
    axis_files = dict(layout_files)
    axis_files['xl/worksheets/sales.xml'] = axis_files['xl/worksheets/sales.xml'].replace(
        '<pane xSplit="1" ySplit="1" topLeftCell="B2" state="frozen"/>', pane)
    with ZipFile(HERE / filename, 'w', ZIP_DEFLATED) as archive:
        for name, body in axis_files.items():
            info = ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            archive.writestr(info, body.encode())

# Dynamic-array caches and named Excel LAMBDAs in a normal XLSX package.
dynamic_files = dict(files)
dynamic_files['xl/workbook.xml'] = dynamic_files['xl/workbook.xml'].replace(
    '</sheets>', '</sheets><definedNames><definedName name="DOUBLE">_xlfn.LAMBDA(_xlpm.x,_xlpm.x*2)</definedName><definedName name="DOUBLE" localSheetId="1">_xlfn.LAMBDA(_xlpm.x,_xlpm.x*3)</definedName></definedNames>')
dynamic_files['xl/worksheets/sales.xml'] = f'''<worksheet xmlns="{X}"><sheetData><row r="1"><c r="A1"><f t="array" ref="A1:B2">_xlfn.SEQUENCE(2,2)</f><v>1</v></c><c r="B1"><v>2</v></c><c r="D1"><f>DOUBLE(6)</f><v>12</v></c></row><row r="2"><c r="A2"><v>3</v></c><c r="B2"><v>4</v></c></row></sheetData></worksheet>'''
dynamic_files['xl/worksheets/notes.xml'] = f'''<worksheet xmlns="{X}"><sheetData><row r="1"><c r="A1"><f>DOUBLE(6)</f><v>18</v></c></row></sheetData></worksheet>'''
with ZipFile(HERE / 'dynamic-arrays.xlsx', 'w', ZIP_DEFLATED) as archive:
    for name, body in dynamic_files.items():
        info = ZipInfo(name, (2020, 1, 1, 0, 0, 0))
        info.compress_type = ZIP_DEFLATED
        archive.writestr(info, body.encode())
