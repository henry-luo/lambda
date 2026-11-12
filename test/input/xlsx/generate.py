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
