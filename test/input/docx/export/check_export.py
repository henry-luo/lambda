#!/usr/bin/env python3
"""Independently inspect exported OOXML and open it with python-docx 1.2.0."""
from pathlib import Path, PurePosixPath
import argparse
import json
import posixpath
import zipfile
import xml.etree.ElementTree as ET
from docx import Document
import docx

W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
R = "http://schemas.openxmlformats.org/package/2006/relationships"
NS = {"w": W, "wp": "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"}
def q(name): return f"{{{W}}}{name}"

ORDERS = {
    "rPr": "rStyle rFonts b bCs i iCs caps smallCaps strike dstrike outline shadow emboss imprint noProof snapToGrid vanish webHidden color spacing w kern position sz szCs highlight u effect bdr shd fitText vertAlign rtl cs em lang eastAsianLayout specVanish oMath rPrChange",
    "pPr": "pStyle keepNext keepLines pageBreakBefore framePr widowControl numPr suppressLineNumbers pBdr shd tabs suppressAutoHyphens kinsoku wordWrap overflowPunct topLinePunct autoSpaceDE autoSpaceDN bidi adjustRightInd snapToGrid spacing ind contextualSpacing mirrorIndents suppressOverlap jc textDirection textAlignment textboxTightWrap outlineLvl divId cnfStyle rPr sectPr pPrChange",
    "tcPr": "cnfStyle tcW gridSpan hMerge vMerge tcBorders shd noWrap tcMar textDirection tcFitText vAlign hideMark headers cellIns cellDel cellMerge tcPrChange",
    "tblPr": "tblStyle tblpPr tblOverlap bidiVisual tblStyleRowBandSize tblStyleColBandSize tblW jc tblCellSpacing tblInd tblBorders shd tblLayout tblCellMar tblLook tblCaption tblDescription tblPrChange",
    "sectPr": "headerReference footerReference footnotePr endnotePr type pgSz pgMar paperSrc pgBorders lnNumType pgNumType cols formProt vAlign noEndnote titlePg textDirection bidi rtlGutter docGrid printerSettings sectPrChange",
    "tcBorders": "top left bottom right insideH insideV",
    "pBdr": "top left bottom right between bar",
    "tcMar": "top left bottom right",
}

def archive_check(path):
    with zipfile.ZipFile(path) as archive:
        assert archive.testzip() is None
        names = archive.namelist()
        assert len(names) == len(set(names))
        assert {"[Content_Types].xml", "_rels/.rels", "word/document.xml", "word/styles.xml", "word/numbering.xml", "docProps/core.xml"} <= set(names)
        xml = {name: ET.fromstring(archive.read(name)) for name in names if name.endswith((".xml", ".rels"))}
        for name, tree in xml.items():
            for node in tree.iter():
                local = node.tag.removeprefix(f"{{{W}}}")
                if local in ORDERS:
                    order = ORDERS[local].split()
                    indices = [order.index(child.tag.removeprefix(f"{{{W}}}")) for child in node]
                    assert indices == sorted(indices), (name, local, indices)
                    assert len(indices) == len(set(indices)), (name, local, "duplicate property")
            if name.endswith(".rels"):
                folder = "" if name == "_rels/.rels" else str(PurePosixPath(name).parent.parent)
                ids = [rel.attrib["Id"] for rel in tree]
                assert len(ids) == len(set(ids))
                for rel in tree:
                    if rel.attrib.get("TargetMode") != "External":
                        target = posixpath.normpath(posixpath.join(folder, rel.attrib["Target"]))
                        assert target in names, (name, target)
        types = xml["[Content_Types].xml"]
        defaults = {node.attrib.get("Extension") for node in types if node.tag.endswith("Default")}
        overrides = {node.attrib.get("PartName") for node in types if node.tag.endswith("Override")}
        assert all(n.endswith("/") or n.rsplit(".", 1)[-1] in defaults or "/" + n in overrides for n in names)
        return xml, names

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=Path("temp/docx-export"))
    args = parser.parse_args()
    folder = args.directory
    xml, names = archive_check(folder / "report.docx")
    doc = Document(folder / "report.docx")
    assert doc.core_properties.title == "Report 世界" and doc.core_properties.author == "Lambda tests"
    heading = next(p for p in doc.paragraphs if p.text == "DOCX export 世界")
    assert heading.style.name == "Heading 1"
    runs = {r.text: r for p in doc.paragraphs for r in p.runs}
    assert runs["bold"].bold and runs["italic"].italic and runs["underline"].underline
    assert runs["strike"].font.strike and runs["sub"].font.subscript and runs["sup"].font.superscript
    styled = runs["Styled & <XML>"]
    assert styled.font.name == "Georgia" and styled.font.size.pt == 18 and str(styled.font.color.rgb) == "2468AC"
    assert styled._r.find("w:rPr/w:shd", NS).attrib[q("fill")] == "fff2cc"
    right = next(p for p in doc.paragraphs if p.text == "Inherited alignment")
    assert int(right.alignment) == 2 and right.paragraph_format.line_spacing == 1.5
    assert len(doc.tables) == 2 and [len(t.rows) for t in doc.tables] == [2, 2]
    table = doc.tables[0]
    assert [[c.text for c in row.cells] for row in table.rows] == [["Merged", "Two columns", "Two columns"], ["Merged", "A", "B"]]
    assert table.rows[0]._tr.find("w:trPr/w:tblHeader", NS) is not None
    assert len(doc.tables[1].rows[1]._tr.findall("w:tc", NS)) == 4
    assert doc.tables[1].cell(1, 3).text == "Far merge"
    assert len(doc.inline_shapes) == 2
    assert [(s.width, s.height) for s in doc.inline_shapes] == [(914400, 457200), (457200, 228600)]
    assert len([n for n in names if n.startswith("word/media/") and not n.endswith("/")]) == 1
    document = xml["word/document.xml"]
    numbering = xml["word/numbering.xml"]
    assert len(numbering.findall("w:num", NS)) == 3
    assert numbering.find("w:abstractNum/w:lvl/w:start", NS).attrib[q("val")] == "3"
    numbers = {"".join(p.itertext()): p.find("w:pPr/w:numPr/w:numId", NS) for p in document.findall("w:body/w:p", NS)}
    assert numbers["Third"].attrib[q("val")] != numbers["Restart at three"].attrib[q("val")]
    assert numbers["Same item continuation"] is None
    assert len(document.findall(".//w:tab", NS)) == 1
    assert len(document.findall(".//w:pageBreakBefore", NS)) == 1
    anchors = {n.attrib[q("name")] for n in document.findall(".//w:bookmarkStart", NS)}
    assert document.findall(".//w:hyperlink", NS)[1].attrib[q("anchor")] in anchors
    links = [r.attrib["Target"] for r in xml["word/_rels/document.xml.rels"] if r.attrib.get("TargetMode") == "External"]
    assert links == ["https://example.com/report?a=1&b=2"]
    assert doc.sections[0].page_width.twips == 11906 and doc.sections[0].left_margin.twips == 1440
    assert (folder / "report.docx").read_bytes() == (folder / "report-copy.docx").read_bytes()
    assert (folder / "protected.docx").read_text() == "previous content"
    archive_check(folder / "word.docx")
    word = Document(folder / "word.docx")
    assert word.core_properties.title == "Exported studio"
    assert len(word.tables) == 1 and len(word.tables[0].rows) == 4
    subtitle = word.paragraphs[1].runs[0]
    assert subtitle.font.name == "Arial" and subtitle.font.size.pt == 12
    quote = next(p for p in word.paragraphs if p.text.startswith("A studio is a place"))
    assert quote.runs[0].italic and quote._p.find("w:pPr/w:pBdr/w:left", NS) is not None
    losses, _ = archive_check(folder / "losses.docx")
    assert "javascript:" not in ET.tostring(losses["word/document.xml"], encoding="unicode")
    assert "Remote image" in "".join(losses["word/document.xml"].itertext())
    print(json.dumps({"result": "PASS", "reader": "python-docx", "version": docx.__version__, "archives": 3,
                      "checks": ["OPC/XML structure and property order", "text and run styles", "numbering and restart", "merged and sparse tables", "pictures", "links and bookmarks", "page geometry", "determinism", "failed-write preservation", "Word sample"]}))

if __name__ == "__main__": main()
